#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include <stdatomic.h>

#include <sys/lock.h>

#include <hardware/platform_defs.h>
#include <hardware/exception.h>
#include <hardware/address_mapped.h>
#include <hardware/regs/sio.h>
#include <pico/rtos/scheduler.h>
#include <pico/tls.h>
#include <pico/retarget-lock.h>

#include "compiler.h"
#include "cmsis.h"

#define LIBC_LOCK_MARKER 0x89988998
//#define MULTICORE

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
void scheduler_run_hook(bool start);
void scheduler_spin_lock(void);
void scheduler_spin_unlock(void);
unsigned int scheduler_spin_lock_irqsave(void);
void scheduler_spin_unlock_irqrestore(unsigned int state);

static atomic_ulong scheduler_spinlock = 0;

static exception_handler_t old_systick_handler = 0;
static exception_handler_t old_pendsv_handler = 0;
static exception_handler_t old_svc_handler = 0;

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


#if 0
struct __lock __lock___libc_recursive_mutex = { 0 };

static void __retarget_lock_init_once(struct __lock *lock)
{
	assert(lock != 0);

	/* All ready done */
	if (atomic_load(&lock->marker) == LIBC_LOCK_MARKER)
		return;

	/* Try to claim the initializer */
	int expected = 0;
	if (!atomic_compare_exchange_strong(&lock->marker, &expected, 1)) {

		/* Wait the the initializer to complete, we sleep to ensure lower priority threads run */
		while (atomic_load(&lock->marker) == LIBC_LOCK_MARKER)
			if (scheduler_is_running())
				scheduler_sleep(10);
			else
				__WFE();

		/* Done */
		return;
	}

	/* Initialize the lock */
	lock->value = 0;
	lock->count = 0;
	scheduler_futex_init(&lock->futex, &lock->value, 0);//SCHEDULER_FUTEX_PI | SCHEDULER_FUTEX_OWNER_TRACKING | SCHEDULER_FUTEX_CONTENTION_TRACKING);

	/*  Mark as done */
	atomic_store(&lock->marker, LIBC_LOCK_MARKER);

	/* Always wake up everyone, no harm done for correct programs */
	__SEV();
}

void __retarget_lock_init(_LOCK_T *lock)
{
	assert(lock != 0);

	/* Get the space for the lock */
	if (*lock == 0) {
		*lock = calloc(1, sizeof(struct __lock));
		if (!*lock)
			abort();
		(*lock)->allocated = true;
	}

	/* Initialize it */
	(*lock)->value = 0;
	(*lock)->count = 0;
	(*lock)->marker = LIBC_LOCK_MARKER;
	scheduler_futex_init(&(*lock)->futex, &(*lock)->value, 0);
}

void __retarget_lock_init_recursive(_LOCK_T *lock)
{
	__retarget_lock_init(lock);
}

void __retarget_lock_close(_LOCK_T lock)
{
	if (lock->allocated)
		free(lock);
}

void __retarget_lock_close_recursive(_LOCK_T lock)
{
	__retarget_lock_close(lock);
}

void __retarget_lock_acquire(_LOCK_T lock)
{
	assert(lock != 0);

	/* Initialize once if needed */
	__retarget_lock_init_once(lock);

	/* Run the lock algo, if the scheduler is not running the task is zero */
	long value = (long)scheduler_task() | (scheduler_current_core() + 1);
	long expected = 0;
	while (!atomic_compare_exchange_strong(&lock->value, &expected, value)) {
		if ((value & 0xfffffffc) != 0) {
			int status = scheduler_futex_wait(&lock->futex, expected, SCHEDULER_WAIT_FOREVER);
			if (status < 0)
				abort();
		} else
			__WFE();

		/* Nope, try again */
		expected = 0;
	}
}

void __retarget_lock_acquire_recursive(_LOCK_T lock)
{
	assert(lock != 0);

	/* Initialize once if needed */
	__retarget_lock_init_once(lock);

	/* Handle recursive locks */
	long value = (long)scheduler_task() | (scheduler_current_core() + 1);
	if (value == atomic_load(&lock->value)) {
		++lock->count;
		return;
	}

	/* Forward */
	__retarget_lock_acquire(lock);

	/* Initialize the count for recursive locks */
	lock->count = 1;
}

int __retarget_lock_try_acquire(_LOCK_T lock)
{
	assert(lock != 0);

	/* Initialize once if needed */
	__retarget_lock_init_once(lock);

	/* Just try update the lock bit */
	long value = (long)scheduler_task() | (scheduler_current_core() + 1);
	long expected = 0;
	if (!atomic_compare_exchange_strong(&lock->value, &expected, value)) {
		errno = EBUSY;
		return false;
	}

	/* Got the lock */
	return true;
}

int __retarget_lock_try_acquire_recursive(_LOCK_T lock)
{
	assert(lock != 0);

	/* Initialize once if needed */
	__retarget_lock_init_once(lock);

	/* Handle recursive locks */
	long value = (long)scheduler_task() | (scheduler_current_core() + 1);
	if (value == atomic_load(&lock->value)) {
		++lock->count;
		return true;
	}

	/* Forward */
	int status = __retarget_lock_try_acquire(lock);
	if (!status)
		return false;

	/* Initialize the count for recursive locks, we have joy */
	lock->count = 1;
	return true;
}

void __retarget_lock_release(_LOCK_T lock)
{
	assert(lock != 0 && (lock->marker == LIBC_LOCK_MARKER));

	/* Unlock, abort if we are not the owner of the lock */
	long expected = (long)scheduler_task() | (scheduler_current_core() + 1);
	if (!atomic_compare_exchange_strong(&lock->value, &expected, 0))
		abort();

	/* If the scheduler is not running we are done */
	if ((expected & 0xfffffffc) == 0)
		return;

	/* Let the waiters contend for the lock */
	int status = scheduler_futex_wake(&lock->futex, false);
	if (status < 0)
		abort();
}

void __retarget_lock_release_recursive(_LOCK_T lock)
{
	assert(lock != 0 && (lock->marker == LIBC_LOCK_MARKER));

	/* Handle recursive lock */
	if (--lock->count > 0)
		return;

	/* Forward */
	__retarget_lock_release(lock);
}

#endif

void scheduler_tls_init_hook(void *tls)
{
	_init_tls(tls);
}

void scheduler_switch_hook(struct task *task)
{
	/* Important to set the tls pointer first */
	_set_tls(task->tls);
}

static void SysTick_Handler(void)
{
	/* Clear the Overflow */
	__unused uint32_t value = SysTick->CTRL;

	/* Forward the to the scheduler tick handler */
	scheduler_tick();
}

void scheduler_run_hook(bool start)
{
	if (start) {

		/* First set the rtos system exception priority */
		NVIC_SetPriority(PendSV_IRQn, SCHEDULER_PENDSV_PRIORITY);
		NVIC_SetPriority(SVCall_IRQn, SCHEDULER_SVC_PRIORITY);
		NVIC_SetPriority(SysTick_IRQn, SCHEDULER_SVC_PRIORITY);

		/* Now install the handlers */
		old_pendsv_handler = exception_set_exclusive_handler(PendSV_IRQn, PendSV_Handler);
		old_svc_handler = exception_set_exclusive_handler(SVCall_IRQn, SVC_Handler);
		old_systick_handler = exception_set_exclusive_handler(SysTick_IRQn, SysTick_Handler);

		/* Save the initial tls pointer */
		cls_datum(old_tls) = __aeabi_read_tp();

		/* Initialize the system tick at 1ms for this core */
		SysTick->LOAD  = (SystemCoreClock / 1000) - 1UL;
		SysTick->VAL   = 0UL;
		SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;

	} else {

		/* Disable the systick */
		SysTick->CTRL = 0;

		/* Restore the initial tls pointer */
		_set_tls(cls_datum(old_tls));

		/* Restore the old handlers */
		exception_restore_handler(SysTick_IRQn, old_systick_handler);
		exception_restore_handler(SVCall_IRQn, old_svc_handler);
		exception_restore_handler(PendSV_IRQn, old_pendsv_handler);
	}
}

#ifdef MULTICORE

void scheduler_spin_lock()
{
	uint16_t ticket = atomic_fetch_add(&scheduler_spinlock, 1UL << 16) >> 16;
	while ((atomic_load(*scheduler_spinlock) & 0xffff) != ticket);
}

void scheduler_spin_unlock(void)
{
	atomic_fetch_add((uint16_t *)scheduler_spinlock, 1);
}

unsigned int scheduler_spin_lock_irqsave(void)
{
	uint32_t state = disable_interrupts();
	scheduler_spin_lock();
	return state;
}

void scheduler_spin_unlock_irqrestore(unsigned int state)
{
	scheduler_spin_unlock(spinlock);
	enable_interrupts(state);
}

unsigned long scheduler_num_cores(void)
{
	return NUM_CORES;
}

unsigned long scheduler_current_core(void)
{
	return *((io_ro_32 *)(SIO_BASE + SIO_CPUID_OFFSET));
}

extern void multicore_post(uintptr_t event);

void scheduler_request_switch(unsigned long core)
{
	/* Current core? */
	if (core == scheduler_current_core()) {
		SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
		return;
	}

	/* System interrupt must be sent directly to the other core */
	multicore_post(0x90000000 | (PendSV_IRQn + 16));
}

static void multicore_trap(void)
{
	abort();
}

void init_fault(void);
void init_fault(void)
{
	multicore_post((uintptr_t)multicore_trap);
}

static void mulitcore_scheduler_run(struct async *async)
{
	/* start the scheduler running */
	scheduler_run();
}
static struct async multicore_scheduler_async;

#endif
