/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * multicore-glue.c
 *
 *  Created on: Mar 14, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdatomic.h>

#include <sys/lock.h>

#include <hardware/platform_defs.h>
#include <hardware/exception.h>
#include <hardware/address_mapped.h>
#include <hardware/regs/sio.h>

#include <pico/toolkit/cmsis.h>
#include <pico/toolkit/scheduler.h>
#include <pico/toolkit/tls.h>
#include <pico/toolkit/retarget-lock.h>


#define LIBC_LOCK_MARKER 0x89988998

struct __rtos_runtime_lock
{
	struct __retarget_runtime_lock retarget_lock;
	struct futex futex;
};

extern void _set_tls(void *tls);
extern void _init_tls(void *__tls_block);
extern void* __aeabi_read_tp(void);
extern void PendSV_Handler(void);
extern void SVC_Handler(void);

void scheduler_switch_hook(struct task *task);
void scheduler_tls_init_hook(void *tls);
void scheduler_startup_hook(void);
void scheduler_shutdown_hook(void);
void scheduler_spin_lock(void);
void scheduler_spin_unlock(void);
unsigned int scheduler_spin_lock_irqsave(void);
void scheduler_spin_unlock_irqrestore(unsigned int state);

extern __weak void multicore_startup_hook(void);
extern __weak void multicore_shutdown_hook(void);

static core_local void *old_tls = { 0 };

static struct __rtos_runtime_lock libc_recursive_mutex = { 0 };
struct __lock __lock___libc_recursive_mutex =
{
	.retarget_lock = &libc_recursive_mutex,
};

void __retarget_runtime_lock_init_once(struct __lock *lock)
{
	assert(lock != 0);

	struct __rtos_runtime_lock *rtos_runtime_lock = lock->retarget_lock;

	/* All ready done */
	if (atomic_load(&rtos_runtime_lock->retarget_lock.marker) == LIBC_LOCK_MARKER)
		return;

	/* Try to claim the initializer */
	int expected = 0;
	if (!atomic_compare_exchange_strong(&rtos_runtime_lock->retarget_lock.marker, &expected, 1)) {

		/* Wait the the initializer to complete, we sleep to ensure lower priority threads run */
		while (atomic_load(&rtos_runtime_lock->retarget_lock.marker) == LIBC_LOCK_MARKER)
			if (scheduler_is_running())
				scheduler_sleep(10);
			else
				__WFE();

		/* Done */
		return;
	}

	/* Initialize the lock */
	rtos_runtime_lock->retarget_lock.value = 0;
	rtos_runtime_lock->retarget_lock.count = 0;
	scheduler_futex_init(&rtos_runtime_lock->futex, &rtos_runtime_lock->retarget_lock.value, 0);

	/*  Mark as done */
	atomic_store(&rtos_runtime_lock->retarget_lock.marker, LIBC_LOCK_MARKER);

	/* Always wake up everyone, no harm done for correct programs */
	__SEV();
}

void __retarget_runtime_lock_init(_LOCK_T *lock)
{
	assert(lock != 0);

	/* Get the space for the lock if needed */
	if (*lock == 0) {

		/* Sigh, we are hiding the type multiple time, I did not make this mess */
		*lock = calloc(1, sizeof(struct __lock) + sizeof(struct __rtos_runtime_lock));
		if (!*lock)
			abort();

		/* Only making it worse */
		struct __rtos_runtime_lock *rtos_runtime_lock = (void *)(*lock) + sizeof(struct __lock);
		rtos_runtime_lock->retarget_lock.allocated = true;
		(*lock)->retarget_lock = rtos_runtime_lock;
	}
}

long __retarget_runtime_lock_value(void)
{
	return (long)scheduler_task() | (scheduler_current_core() + 1);
}

void __retarget_runtime_relax(_LOCK_T lock)
{
	struct __rtos_runtime_lock *rtos_runtime_lock = lock->retarget_lock;

	if ((__retarget_runtime_lock_value() & 0xfffffffc) != 0) {
		int status = scheduler_futex_wait(&rtos_runtime_lock->futex, rtos_runtime_lock->retarget_lock.expected, SCHEDULER_WAIT_FOREVER);
		if (status < 0)
			abort();
		return;
	}

	/* Scheduler is not running, wait for an core event */
	__WFE();
}

void __retarget_runtime_wake(_LOCK_T lock)
{
	struct __rtos_runtime_lock *rtos_runtime_lock = lock->retarget_lock;

	/* If the scheduler is not running we are done, just do a core event */
	if ((rtos_runtime_lock->retarget_lock.expected & 0xfffffffc) == 0) {
		__SEV();
		return;
	}

	/* Let the waiters contend for the lock */
	int status = scheduler_futex_wake(&rtos_runtime_lock->futex, false);
	if (status < 0)
		abort();
}

__weak void scheduler_tls_init_hook(void *tls)
{
	_init_tls(tls);
}

void scheduler_switch_hook(struct task *task)
{
	_set_tls(task != 0 ? task->tls : 0);
}

static void SysTick_Handler(void)
{
	/* Clear the Overflow */
	__unused uint32_t value = SysTick->CTRL;

	/* Forward the to the scheduler tick handler */
	scheduler_tick();
}

void scheduler_startup_hook(void)
{
	/* First set the rtos system exception priority, done this way SDK does not support setting the system irq priorities */
	NVIC_SetPriority(PendSV_IRQn, SCHEDULER_PENDSV_PRIORITY);
	NVIC_SetPriority(SVCall_IRQn, SCHEDULER_SVC_PRIORITY);
	NVIC_SetPriority(SysTick_IRQn, SCHEDULER_SYSTICK_PRIORITY);

	/* Disable deep sleep wake and generate a SEV on pending interrupts*/
	SCB->SCR = SCB_SCR_SEVONPEND_Msk;

	/* We need to install a systick handler */
	exception_set_exclusive_handler(SYSTICK_EXCEPTION, SysTick_Handler);

	/* Save the initial tls pointer */
	cls_datum(old_tls) = __aeabi_read_tp();

	/* Initialize the system tick at 1ms for this core */
	SysTick->LOAD  = (SystemCoreClock / 1000) - 1UL;
	SysTick->VAL   = 0UL;
	SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;

	/* Optionally pass to the multicore hook */
	multicore_startup_hook();
}

void scheduler_shutdown_hook(void)
{
	/* Disable the systick */
	SysTick->CTRL = 0;

	/* Restore the initial tls pointer */
	_set_tls(cls_datum(old_tls));

	/* Optionally pass to the multicore hook */
	multicore_shutdown_hook();
}
