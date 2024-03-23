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

#include <pico/toolkit/cmsis.h>

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

		/* We need to install a systick handler */
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

		/* Restore the old systick handler */
		exception_restore_handler(SysTick_IRQn, old_systick_handler);
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
