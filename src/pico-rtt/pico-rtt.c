/*
 * pico-rtt.c
 *
 *  Created on: Apr 22, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <pico/toolkit/spinlock.h>

spinlock_t rtt_spinlock = 0;

