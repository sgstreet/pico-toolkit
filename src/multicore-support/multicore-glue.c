/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * multicore-glue.c
 *
 *  Created on: Mar 26, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>

#include <hardware/structs/syscfg.h>
#include <hardware/regs/sio.h>
#include <hardware/irq.h>
#include <hardware/sync.h>

#include <pico/platform.h>
#include <pico/multicore.h>
#include <pico/bootrom.h>

#include <pico/toolkit/cmsis.h>
#include <pico/toolkit/multicore-irq.h>
#include <pico/toolkit/scheduler.h>
#include <pico/toolkit/tls.h>

extern void scheduler_startup_hook(void);

void init_fault(void);
void multicore_run_hook(bool start);

static spin_lock_t *lock = (spin_lock_t *)(SIO_BASE + SIO_SPINLOCK0_OFFSET + PICO_SPINLOCK_ID_OS1 * 4);

void scheduler_spin_lock()
{
    while (__builtin_expect(!*lock, 0))
    	__WFE();
    __mem_fence_acquire();
}

void scheduler_spin_unlock(void)
{
    __mem_fence_release();
    *lock = 0;
	__SEV();
}

unsigned int scheduler_spin_lock_irqsave(void)
{
	return spin_lock_blocking(lock);
}

void scheduler_spin_unlock_irqrestore(unsigned int state)
{
	spin_unlock(lock, state);
}

unsigned long scheduler_num_cores(void)
{
	return NUM_CORES;
}

unsigned long scheduler_current_core(void)
{
	return get_core_num();
}

void scheduler_request_switch(unsigned long core)
{
	multicore_irq_set_pending(PendSV_IRQn, core);
}

static void multicore_trap(void)
{
	while (true);
}

void init_fault(void)
{
	multicore_fifo_push_blocking((uintptr_t)multicore_trap);
}

static void multicore_start(void)
{
	/* This will capture the initial frame, we will return here when the scheduler exits, see  */
	scheduler_run();

	/* Got back into the rom wait for vector */
	((void (*)(void))rom_func_lookup(rom_table_code('W', 'V')))();
}

void multicore_startup_hook(void)
{
	/* If we are running on the startup core, launch core 1 */
	if (get_core_num() == 0) {

		/* Disable the processor interrupts */
		atomic_fetch_and(&syscfg_hw->proc0_nmi_mask, ~(1UL << SIO_IRQ_PROC0));
		atomic_fetch_and(&syscfg_hw->proc1_nmi_mask, ~(1UL << SIO_IRQ_PROC1));

		/* Launch the start up on core 1 */
		multicore_launch_core1(multicore_start);

		/* Spin until the nmi mask is set again */
		while ((atomic_load(&syscfg_hw->proc1_nmi_mask) & (1UL << SIO_IRQ_PROC1)) == 0);

		/* Re-enable the core zero interrupt */
		atomic_fetch_or(&syscfg_hw->proc0_nmi_mask, (1UL << SIO_IRQ_PROC0));

	} else
		/* Let's will release the core zero startup */
		atomic_fetch_or(&syscfg_hw->proc1_nmi_mask, (1UL << SIO_IRQ_PROC1));
}

void multicore_shutdown_hook(void)
{
}
