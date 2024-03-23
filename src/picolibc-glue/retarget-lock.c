/*
 * retarget-lock.c
 *
 *  Created on: Mar 19, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdatomic.h>

#include <pico/toolkit/cmsis.h>

#include <pico/platform.h>
#include <pico/retarget-lock.h>

#ifdef _RETARGETABLE_LOCKING

static struct __retarget_runtime_lock libc_recursive_mutex = { 0 };
__weak struct __lock __lock___libc_recursive_mutex =
{
	.retarget_lock = &libc_recursive_mutex,
};

__weak void __retarget_runtime_lock_init_once(_LOCK_T lock)
{
	assert(lock != 0);

	struct __retarget_runtime_lock *runtime_lock = lock->retarget_lock;

	/* All ready done */
	if (atomic_load(&runtime_lock->marker) == LIBC_LOCK_MARKER)
		return;

	/* Try to claim the initializer */
	int expected = 0;
	if (!atomic_compare_exchange_strong(&runtime_lock->marker, &expected, 1)) {

		/* Wait the the initializer to complete, we sleep to ensure lower priority threads run */
		while (atomic_load(&runtime_lock->marker) == LIBC_LOCK_MARKER)
			__WFE();

		/* Done */
		return;
	}

	/* Initialize the lock */
	runtime_lock->value = 0;
	runtime_lock->count = 0;

	/*  Mark as done */
	atomic_store(&runtime_lock->marker, LIBC_LOCK_MARKER);

	/* Always wake up everyone, no harm done for correct programs */
	__SEV();
}

__weak void __retarget_runtime_lock_init(_LOCK_T *lock)
{
	assert(lock != 0);

	/* Get the space for the lock if needed */
	if (*lock == 0) {

		/* Sigh, we are hiding the type multiple time, I did not make this mess */
		*lock = calloc(1, sizeof(struct __lock) + sizeof(struct __retarget_runtime_lock));
		if (!*lock)
			abort();

		/* Only making it worse */
		struct __retarget_runtime_lock *runtime_lock = (void *)(*lock) + sizeof(struct __lock);
		runtime_lock->allocated = true;
		(*lock)->retarget_lock = runtime_lock;
	}
}

__weak void __retarget_runtime_relax(_LOCK_T lock)
{
	__WFE();
}

__weak void __retarget_runtime_wake(_LOCK_T lock)
{
	__SEV();
}

__weak void __retarget_runtime_lock_close(_LOCK_T lock)
{
	assert(lock != 0);

	struct __retarget_runtime_lock *runtime_lock = lock->retarget_lock;

	if (runtime_lock->allocated)
		free(lock);
}

__weak long __retarget_runtime_lock_value(void)
{
	return get_core_num() + 1;
}

__weak int __retarget_runtime_lock_try_acquire(_LOCK_T lock)
{
	assert(lock != 0);

	struct __retarget_runtime_lock *runtime_lock = lock->retarget_lock;

	/* Handle recursive locks */
	long value = __retarget_runtime_lock_value();
	if (value == atomic_load(&runtime_lock->value)) {
		++runtime_lock->count;
		return true;
	}

	/* Try to get the lock */
	runtime_lock->expected = 0;
	if (!atomic_compare_exchange_strong(&runtime_lock->value, &runtime_lock->expected, value)) {
		errno = EBUSY;
		return false;
	}

	/* Initialize the count for recursive locks, we have joy */
	runtime_lock->count = 1;
	return true;
}

__weak void __retarget_runtime_lock_release(_LOCK_T lock)
{
	assert(lock != 0);

	struct __retarget_runtime_lock *runtime_lock = lock->retarget_lock;

	/* Handle recursive lock */
	if (atomic_load(&runtime_lock->value) == __retarget_runtime_lock_value() && --runtime_lock->count > 0)
		return;

	/* Unlock, abort if we are not the owner of the lock */
	runtime_lock->expected = __retarget_runtime_lock_value();
	if (!atomic_compare_exchange_strong(&runtime_lock->value, &runtime_lock->expected, 0))
		abort();
}

void __retarget_lock_init(_LOCK_T *lock)
{
	__retarget_runtime_lock_init(lock);
}
__alias("__retarget_lock_init") void __retarget_lock_init_recursive(_LOCK_T *lock);

void __retarget_lock_close(_LOCK_T lock)
{
	__retarget_runtime_lock_close(lock);
}
__alias("__retarget_lock_close") void __retarget_lock_close_recursive(_LOCK_T lock);

void __retarget_lock_acquire(_LOCK_T lock)
{
	assert(lock != 0);

	/* Initialize once if needed */
	__retarget_runtime_lock_init_once(lock);

	/* Wait for the lock */
	while (!__retarget_runtime_lock_try_acquire(lock))
		__retarget_runtime_relax(lock);
}
__alias("__retarget_lock_acquire") void __retarget_lock_acquire_recursive(_LOCK_T lock);

int __retarget_lock_try_acquire(_LOCK_T lock)
{
	/* Initialize once if needed */
	__retarget_runtime_lock_init_once(lock);

	return __retarget_runtime_lock_try_acquire(lock);
}
__alias("__retarget_lock_try_acquire") int __retarget_lock_try_acquire_recursive(_LOCK_T lock);

void __retarget_lock_release(_LOCK_T lock)
{
	__retarget_runtime_lock_release(lock);

	__retarget_runtime_wake(lock);
}
__alias("__retarget_lock_release") void __retarget_lock_release_recursive(_LOCK_T lock);

#endif
