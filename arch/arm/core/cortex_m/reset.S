/*
 * Copyright (c) 2013-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Reset handler
 *
 * Reset handler that prepares the system for running C code.
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <zephyr/arch/cpu.h>
#include "vector_table.h"

_ASM_FILE_PROLOGUE

GTEXT(z_arm_reset)
GTEXT(z_early_memset)
GDATA(z_interrupt_stacks)
#if defined(CONFIG_DEBUG_THREAD_INFO)
GDATA(z_sys_post_kernel)
#endif
#if defined(CONFIG_PLATFORM_SPECIFIC_INIT)
GTEXT(z_arm_platform_init)
#endif
#if defined(CONFIG_INIT_ARCH_HW_AT_BOOT)
GTEXT(z_arm_init_arch_hw_at_boot)
GDATA(z_main_stack)
#endif
#if defined(CONFIG_PM_S2RAM)
GTEXT(arch_pm_s2ram_resume)
#endif

/**
 *
 * @brief Reset vector
 *
 * Ran when the system comes out of reset, or when the firmware image is chain-
 * loaded by another application (for instance, a bootloader). At minimum, the
 * processor must be in thread mode with privileged level. At this point, the
 * main stack pointer (MSP) should be already pointing to a valid area in SRAM.
 *
 * Locking interrupts prevents anything but NMIs and hard faults from
 * interrupting the CPU. A default NMI handler is already in place in the
 * vector table, and the boot code should not generate hard fault, or we're in
 * deep trouble.
 *
 * We want to use the process stack pointer (PSP) instead of the MSP, since the
 * MSP is to be set up to point to the one-and-only interrupt stack during
 * later boot. That would not be possible if in use for running C code.
 *
 * When these steps are completed, jump to z_arm_prep_c(), which will finish
 * setting up the system for running C code.
 *
 */

SECTION_SUBSEC_FUNC(TEXT,_reset_section,z_arm_reset)

/*
 * The entry point is located at the z_arm_reset symbol, which
 * is fetched by a XIP image playing the role of a bootloader, which jumps to
 * it, not through the reset vector mechanism. Such bootloaders might want to
 * search for a __start symbol instead, so create that alias here.
 */
SECTION_SUBSEC_FUNC(TEXT,_reset_section,__start)
#if defined(CONFIG_DEBUG_THREAD_INFO)
    /* Clear z_sys_post_kernel flag for RTOS aware debuggers */
    movs.n r0, #0
    ldr r1, =z_sys_post_kernel
    strb r0, [r1]
#endif /* CONFIG_DEBUG_THREAD_INFO */

#if defined(CONFIG_INIT_ARCH_HW_AT_BOOT)
    /* Reset CONTROL register */
    movs.n r0, #0
    msr CONTROL, r0
    isb
#if defined(CONFIG_CPU_CORTEX_M_HAS_SPLIM)
    /* Clear SPLIM registers */
    movs.n r0, #0
    msr MSPLIM, r0
    msr PSPLIM, r0
#endif /* CONFIG_CPU_CORTEX_M_HAS_SPLIM */

#endif /* CONFIG_INIT_ARCH_HW_AT_BOOT */

#if defined(CONFIG_PM_S2RAM)
    bl arch_pm_s2ram_resume
#endif /* CONFIG_PM_S2RAM */

#if defined(CONFIG_PLATFORM_SPECIFIC_INIT)
#ifndef CONFIG_SOC_INGCHIPS_ING9168
    bl z_arm_platform_init
#endif
#endif

#if defined(CONFIG_INIT_ARCH_HW_AT_BOOT)
#if defined(CONFIG_CPU_HAS_ARM_MPU)
    /* Disable MPU */
    movs.n r0, #0
    ldr r1, =_SCS_MPU_CTRL
    str r0, [r1]
    dsb
#endif /* CONFIG_CPU_HAS_ARM_MPU */
    ldr r0, =z_main_stack + CONFIG_MAIN_STACK_SIZE
    msr msp, r0

    /* Initialize core architecture registers and system blocks */
    bl z_arm_init_arch_hw_at_boot
#endif /* CONFIG_INIT_ARCH_HW_AT_BOOT */

    /* lock interrupts: will get unlocked when switch to main task */
#if defined(CONFIG_ARMV6_M_ARMV8_M_BASELINE)
    cpsid i
#elif defined(CONFIG_ARMV7_M_ARMV8_M_MAINLINE)
    movs.n r0, #_EXC_IRQ_DEFAULT_PRIO
    msr BASEPRI, r0
#else
#error Unknown ARM architecture
#endif

#ifdef CONFIG_WDOG_INIT
    /* board-specific watchdog initialization is necessary */
    bl z_arm_watchdog_init
#endif

/*
 *
 * Note: in all Cortex-M variants the interrupt stack area is at
 * least equal to CONFIG_ISR_STACK_SIZE + MPU_GUARD_ALIGN_AND_SIZE
 * (may be larger due to rounding up for stack pointer aligning
 * purposes but this is sufficient during initialization).
 */

#ifdef CONFIG_INIT_STACKS
    ldr r0, =z_interrupt_stacks
    ldr r1, =0xaa
    ldr r2, =CONFIG_ISR_STACK_SIZE + MPU_GUARD_ALIGN_AND_SIZE
    bl z_early_memset
#endif

    /*
     * Set PSP and use it to boot without using MSP, so that it
     * gets set to z_interrupt_stacks during initialization.
     */
    ldr r0, =z_interrupt_stacks
    ldr r1, =CONFIG_ISR_STACK_SIZE + MPU_GUARD_ALIGN_AND_SIZE
    adds r0, r0, r1
    msr PSP, r0
#ifndef CONFIG_SOC_INGCHIPS_ING9168
    mrs r0, CONTROL
    movs r1, #2
    orrs r0, r1 /* CONTROL_SPSEL_Msk */
    msr CONTROL, r0
    /*
     * When changing the stack pointer, software must use an ISB instruction
     * immediately after the MSR instruction. This ensures that instructions
     * after the ISB instruction execute using the new stack pointer.
     */
    isb
#endif

    /*
     * 'bl' jumps the furthest of the branch instructions that are
     * supported on all platforms. So it is used when jumping to z_arm_prep_c
     * (even though we do not intend to return).
     */

#ifndef CONFIG_SOC_INGCHIPS_ING9168
    bl z_arm_prep_c
#else 
Reset_Handler:

    push    {r1, lr}

/*  Single section scheme.
 *
 *  The ranges of copy from/to are specified by following symbols
 *    __etext: LMA of start of the section to copy from. Usually end of text
 *    __data_start__: VMA of start of the section to copy to
 *    __data_end__: VMA of end of the section to copy to
 *
 *  All addresses must be aligned to 4 bytes boundary.
 */
    ldr    r1, = __data_load_start
    ldr    r2, =_image_ram_start
    ldr    r3, =__kernel_ram_end

.L_loop1:
    cmp    r2, r3
    ittt    lt
    ldrlt    r0, [r1], #4
    strlt    r0, [r2], #4
    blt    .L_loop1
/*  Single BSS section scheme.
 *
 *  The BSS section is specified by following symbols
 *    __bss_start__: start of the BSS section.
 *    __bss_end__: end of the BSS section.
 *
 *  Both addresses must be aligned to 4 bytes boundary.
 */
    ldr    r1, =__bss_start
    ldr    r2, =__bss_end
    movs    r0, 0
.L_loop3:
    cmp    r1, r2
    itt    lt
    strlt    r0, [r1], #4
    blt    .L_loop3

    
    ldr     r0, =app_main
    blx     r0

    pop     {r1, pc}

    .pool
    .size    Reset_Handler, . - Reset_Handler
#endif
