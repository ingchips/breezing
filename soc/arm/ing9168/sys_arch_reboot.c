/*
 * Copyright (c) 2013-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "platform_api.h"
/**
 *
 * @brief Reset the system
 *
 * This routine resets the processor.
 *
 */

void sys_arch_reboot(int type)
{
	(void)(type);

    // FIXME: SYS_REBOOT_WARM is not supported.

    platform_reset();
}
