#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <pico/toolkit/compiler.h>

#include <cmsis/cmsis-rtos2.h>

extern void *_rtos2_alloc(size_t size);
extern void _rtos2_release(void *ptr);

extern __weak struct rtos_semaphore *_rtos2_alloc_semaphore(void);
extern __weak void _rtos2_release_semaphore(struct rtos_semaphore *semaphore);

__weak struct rtos_semaphore *_rtos2_alloc_semaphore(void)
{
	return _rtos2_alloc(sizeof(struct rtos_semaphore));
}

__weak void _rtos2_release_semaphore(struct rtos_semaphore *semaphore)
{
	_rtos2_release(semaphore);
}

osSemaphoreId_t osSemaphoreNew(uint32_t max_count, uint32_t initial_count, const osSemaphoreAttr_t *attr)
{
	const osSemaphoreAttr_t default_attr = { .name = "" };

	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Check for attribute */
	if (!attr)
		attr = &default_attr;

	/* Setup the semaphore memory and validate the size*/
	struct rtos_semaphore *new_semaphore = attr->cb_mem;
	if (!new_semaphore) {
		new_semaphore = _rtos2_alloc_semaphore();
		if (!new_semaphore)
			return 0;
	} else if (attr->cb_size < sizeof(struct rtos_semaphore))
		return 0;

	/* Initialize */
	new_semaphore->marker = RTOS_SEMAPHORE_MARKER;
	strncpy(new_semaphore->name, (attr->name == 0 ? default_attr.name : attr->name), RTOS_NAME_SIZE);
	new_semaphore->name[RTOS_NAME_SIZE - 1] = 0;
	new_semaphore->attr_bits = attr->attr_bits | (new_semaphore != attr->cb_mem ? osDynamicAlloc : 0);
	new_semaphore->max_count = max_count;
	new_semaphore->value = initial_count;
	scheduler_futex_init(&new_semaphore->futex, (long *)&new_semaphore->value, 0);
	list_init(&new_semaphore->resource_node);

	/* Add the new semaphore to the resource list */
	if (osKernelResourceAdd(osResourceSemaphore, &new_semaphore->resource_node) != osOK) {

		/* Only release dynamically allocation */
		if (new_semaphore->attr_bits & osDynamicAlloc)
			_rtos2_release_semaphore(new_semaphore);

		/* This only happen when the resource locking fails */
		return 0;
	}

	/* All good */
	return new_semaphore;
}

const char *osSemaphoreGetName(osSemaphoreId_t semaphore_id)
{
	/* Validate the context */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the semaphore */
	os_status = osIsResourceValid(semaphore_id, RTOS_SEMAPHORE_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_semaphore *semaphore = semaphore_id;

	/* Return the name */
	return strlen(semaphore->name) > 0 ? semaphore->name : 0;
}

osStatus_t osSemaphoreAcquire(osSemaphoreId_t semaphore_id, uint32_t timeout)
{
	/* Validate the context */
	osStatus_t os_status = osKernelContextIsValid(true, timeout);
	if (os_status != osOK)
		return os_status;

	/* Validate the semaphore */
	os_status = osIsResourceValid(semaphore_id, RTOS_SEMAPHORE_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_semaphore *semaphore = semaphore_id;

	/* Run the acquire algo, this is tricky, the happy path does 2 compare exchanges,
	 * The first checks to zero and the second decrements the counter, the whole thing
	 * depends on the how expected is handled */
	uint32_t expected = 1;
	while (!atomic_compare_exchange_weak(&semaphore->value, &expected, expected - 1)) {

		/* No resources */
		if (expected == 0) {

			/* If try semantics, we are done */
			if (timeout == 0)
				return osErrorResource;

			/* We need to wait */
			int status = scheduler_futex_wait(&semaphore->futex, 0, timeout);
			if (status < 0)
				return status == -ETIMEDOUT || status == -ECANCELED ? osErrorTimeout : osError;

			/* Try again */
			expected = 1;
		}
	}

	/* Got a token */
	return osOK;
}

osStatus_t osSemaphoreRelease(osSemaphoreId_t semaphore_id)
{
	/* Validate the context */
	osStatus_t os_status = osKernelContextIsValid(true, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the semaphore */
	os_status = osIsResourceValid(semaphore_id, RTOS_SEMAPHORE_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_semaphore *semaphore = semaphore_id;

	/* Already at the max count? */
	if (semaphore->value >= semaphore->max_count)
		return osErrorResource;

	/* Run the algo */
    if (atomic_fetch_add(&semaphore->value, 1) == 0)
		scheduler_futex_wake(&semaphore->futex, false);

    /* Looks good */
    return osOK;
}

uint32_t osSemaphoreGetCount(osSemaphoreId_t semaphore_id)
{
	/* Validate the context */
	osStatus_t os_status = osKernelContextIsValid(true, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the semaphore */
	os_status = osIsResourceValid(semaphore_id, RTOS_SEMAPHORE_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_semaphore *semaphore = semaphore_id;

	/* Return the name */
	return semaphore->value;
}

osStatus_t osSemaphoreDelete(osSemaphoreId_t semaphore_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the semaphore */
	os_status = osIsResourceValid(semaphore_id, RTOS_SEMAPHORE_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_semaphore *semaphore = semaphore_id;

	/* Clear the marker */
	semaphore->marker = 0;

	/* Remove the semaphore to the resource list */
	os_status = osKernelResourceRemove(osResourceSemaphore, &semaphore->resource_node);
	if (os_status != osOK)
		return os_status;

	/* Free the memory if the is dynamically allocated */
	if (semaphore->attr_bits & osDynamicAlloc)
		_rtos2_release_semaphore(semaphore);

	/* Yea, yea, done */
	return osOK;
}
