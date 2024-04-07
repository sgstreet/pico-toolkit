#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <pico/toolkit/compiler.h>

#include <cmsis/cmsis-rtos2.h>

extern void *_rtos2_alloc(size_t size);
extern void _rtos2_release(void *ptr);

extern __weak struct rtos_mutex *_rtos2_alloc_mutex(void);
extern __weak void _rtos2_release_mutex(struct rtos_mutex *mutex);

__weak struct rtos_mutex *_rtos2_alloc_mutex(void)
{
	return _rtos2_alloc(sizeof(struct rtos_mutex));
}

__weak void _rtos2_release_mutex(struct rtos_mutex *mutex)
{
	_rtos2_release(mutex);
}

osMutexId_t osMutexNew(const osMutexAttr_t *attr)
{
	const osMutexAttr_t default_attr = { .name = "" };

	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Check for attribute */
	if (!attr)
		attr = &default_attr;

	/* Setup the mutex memory and validate the size*/
	struct rtos_mutex *new_mutex = attr->cb_mem;
	if (!new_mutex) {
		new_mutex = _rtos2_alloc_mutex();
		if (!new_mutex)
			return 0;
	} else if (attr->cb_size < sizeof(struct rtos_mutex))
		return 0;

	/* Initialize */
	new_mutex->marker = RTOS_MUTEX_MARKER;
	strncpy(new_mutex->name, (attr->name == 0 ? default_attr.name : attr->name), RTOS_NAME_SIZE);
	new_mutex->name[RTOS_NAME_SIZE - 1] = 0;
	new_mutex->attr_bits = attr->attr_bits | (new_mutex != attr->cb_mem ? osDynamicAlloc : 0);
	scheduler_futex_init(&new_mutex->futex, (long *)&new_mutex->value, (new_mutex->attr_bits & osMutexPrioInherit) ? SCHEDULER_FUTEX_PI | SCHEDULER_FUTEX_OWNER_TRACKING | SCHEDULER_FUTEX_CONTENTION_TRACKING : SCHEDULER_FUTEX_OWNER_TRACKING | SCHEDULER_FUTEX_CONTENTION_TRACKING);
	new_mutex->count = 0;
	list_init(&new_mutex->resource_node);

	/* Add the new mutex to the resource list */
	if (osKernelResourceAdd((new_mutex->attr_bits & osMutexRobust) ? osResourceRobustMutex : osResourceMutex, &new_mutex->resource_node) != osOK) {

		/* Only release dynamically allocation */
		if (new_mutex->attr_bits & osDynamicAlloc)
			_rtos2_release_mutex(new_mutex);

		/* This only happen when the resource locking fails */
		return 0;
	}

	/* All good */
	return new_mutex;
}

const char *osMutexGetName(osMutexId_t mutex_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the mutex */
	os_status = osIsResourceValid(mutex_id, RTOS_MUTEX_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_mutex *mutex = mutex_id;

	/* Return the name */
	return strlen(mutex->name) > 0 ? mutex->name : 0;
}

osStatus_t osMutexAcquire(osMutexId_t mutex_id, uint32_t timeout)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the mutex */
	os_status = osIsResourceValid(mutex_id, RTOS_MUTEX_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_mutex *mutex = mutex_id;

	/* Handle recursive locks */
	long value = (long)scheduler_task();
	if (value == (mutex->value & ~SCHEDULER_FUTEX_CONTENTION_TRACKING)) {
		if ((mutex->attr_bits & osMutexRecursive) == 0)
			return osErrorParameter;
		++mutex->count;
		return osOK;
	}

	/* Run the lock algo */
	long expected = 0;
	while (!atomic_compare_exchange_strong(&mutex->value, &expected, value)) {

		/* Try sematics? */
		if (timeout == 0)
			return osErrorResource;

		/* Nope wait for the lock */
		int status = scheduler_futex_wait(&mutex->futex, expected, timeout);
		if (status < 0)
			return status == -ETIMEDOUT || status == -ECANCELED ? osErrorTimeout : osError;

		/* We have requested contention tracking, we might own the mutex now */
		if (value == (mutex->value & ~SCHEDULER_FUTEX_CONTENTION_TRACKING))
			break;

		/* No we did not end up ownership, try again */
		expected = 0;
	}

	/* Initialize the count for recursive locks */
	if (mutex->attr_bits & osMutexRecursive)
		mutex->count = 1;

	/* Locked */
	return osOK;
}

osStatus_t osMutexRelease(osMutexId_t mutex_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the mutex */
	os_status = osIsResourceValid(mutex_id, RTOS_MUTEX_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_mutex *mutex = mutex_id;

	/* Make sure we are the locker */
	if (osMutexGetOwner(mutex_id) != osThreadGetId())
		return osErrorResource;

	/* Handle recursive lock */
	if ((mutex->attr_bits & osMutexRecursive) && --mutex->count > 0)
		return osOK;

	/* Hot path unlock in the non-contended case */
	long expected = (long)scheduler_task();
	if (mutex->value == expected && atomic_compare_exchange_strong(&mutex->value, &expected, 0))
		return osOK;

	/* Must have been contended */
	int status = scheduler_futex_wake(&mutex->futex, false);
	if (status < 0)
		return osError;

	/* All good */
	return osOK;
}

osStatus_t osMutexRobustRelease(osMutexId_t mutex_id, osThreadId_t owner)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the mutex */
	os_status = osIsResourceValid(mutex_id, RTOS_MUTEX_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_mutex *mutex = mutex_id;

	/* Validate the owner */
	os_status = osIsResourceValid(owner, RTOS_THREAD_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_thread *thread = owner;

	/* Make sure we are the locker */
	if (osMutexGetOwner(mutex_id) != owner)
		return osErrorResource;

	/* Handle recursive lock */
	if (mutex->attr_bits & osMutexRecursive)
		mutex->count = 0;

	/* Hot path unlock in the non-contented case */
	long expected = (long)thread->stack;
	if (mutex->value == expected && atomic_compare_exchange_strong(&mutex->value, &expected, 0))
		return osOK;

	/* Must have been contended */
	int status = scheduler_futex_wake(&mutex->futex, false);
	if (status < 0)
		return osError;

	/* All good */
	return osOK;
}

osThreadId_t osMutexGetOwner(osMutexId_t mutex_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the mutex */
	os_status = osIsResourceValid(mutex_id, RTOS_MUTEX_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_mutex *mutex = mutex_id;

	/* Is it locked? */
	long value = mutex->value;
	if (value == 0)
		return 0;

	/* Extract and validate the task */
	struct task *task = (struct task*)(value & ~SCHEDULER_FUTEX_CONTENTION_TRACKING);
	if (task->marker != SCHEDULER_TASK_MARKER)
		abort();

	/* Our thread is in the context */
	return task->context;
}

osStatus_t osMutexDelete(osMutexId_t mutex_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the mutex */
	os_status = osIsResourceValid(mutex_id, RTOS_MUTEX_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_mutex *mutex = mutex_id;

	/* Clear the marker */
	mutex->marker = 0;

	/* Remove the mutex to the resource list */
	os_status = osKernelResourceRemove((mutex->attr_bits & osMutexRobust) ? osResourceRobustMutex : osResourceMutex, &mutex->resource_node);
	if (os_status != osOK)
		return os_status;

	/* Free the memory if the is dynamically allocated */
	if (mutex->attr_bits & osDynamicAlloc)
		_rtos2_release_mutex(mutex);

	/* Yea, yea, done */
	return osOK;
}
