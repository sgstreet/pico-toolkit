/*
 * Copyright (C) 2022 ARM Limited or its affiliates. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef RV2_CONFIG_DEVICE_H__
#define RV2_CONFIG_DEVICE_H__

#include <pico/toolkit/cmsis.h>

/* Primary interrupt handler */
#ifndef TST_IRQ_HANDLER_A
#define TST_IRQ_HANDLER_A   Interrupt26_Handler
#endif
#ifndef TST_IRQ_NUM_A
#define TST_IRQ_NUM_A       26
#endif

/* Secondary interrupt handler */
#ifndef TST_IRQ_HANDLER_B
#define TST_IRQ_HANDLER_B   Interrupt27_Handler
#endif
#ifndef TST_IRQ_NUM_B
#define TST_IRQ_NUM_B       27
#endif

#endif /* RV2_CONFIG_DEVICE_H__ */
