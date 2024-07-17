/*
 * Copyright (c) 2024 INGCHIPS
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <soc.h>
#include <zephyr/init.h>
#include "platform_api.h"
#include <cmsis_core.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(soc, CONFIG_SOC_LOG_LEVEL);


void set_mode_stop(uint8_t substate_id)
{
    LOG_DBG("Unsupported power state substate-id %u", substate_id);
}

#define CYC_PER_TICK (sys_clock_hw_cycles_per_sec()	/ CONFIG_SYS_CLOCK_TICKS_PER_SEC)

#define EXPECTED_IDLE_TIME_BEFORE_SLEEP     5

/* Invoke Low Power/System Off specific Tasks */
void pm_state_set(enum pm_state state, uint8_t substate_id)
{
    uint32_t timeout_tick = 0x00ffffff / CYC_PER_TICK;
    LOG_DBG("pm state set state[%d],id[%u]", state, substate_id);

    if (SysTick->CTRL & SysTick_CTRL_ENABLE_Msk)
    {
        while (SysTick->VAL == 0) ;
        timeout_tick = SysTick->VAL / CYC_PER_TICK;
    }

    if (timeout_tick < 35)
    {
        k_cpu_idle();
        return;
    }

    timeout_tick -= 2;

    timeout_tick = platform_pre_suppress_ticks_and_sleep_processing(timeout_tick);

    uint32_t pmsk = __get_PRIMASK();
    __disable_irq();
    arch_irq_unlock(0);

    platform_pre_sleep_processing();
    platform_post_sleep_processing();

    __set_PRIMASK(pmsk);
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
}

/* Handle SOC specific activity after Low Power Mode Exit */
void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
    platform_os_idle_resumed_hook();
}