/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * cmsis.h
 *
 *  Created on: Mar 23, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#ifndef _CMSIS_H_
#define _CMSIS_H_

#include <RP2040.h>

#include <pico/toolkit/compiler.h>

static __always_inline inline uint32_t disable_interrupts(void)
{
	uint32_t primask = __get_PRIMASK();
	__set_PRIMASK(1);
	return primask;
}

static __always_inline inline void enable_interrupts(uint32_t primask)
{
	__set_PRIMASK(primask);
}

#endif
