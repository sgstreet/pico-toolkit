/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * pico-rtt.c
 *
 *  Created on: Apr 22, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <pico/toolkit/spinlock.h>

spinlock_t rtt_spinlock = 0;

