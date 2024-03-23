#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <pico/toolkit/compiler.h>

#include <cmsis/cmsis-rtos2.h>

extern void *_rtos2_alloc(size_t size);
extern void _rtos2_release(void *ptr);

extern __weak struct rtos_eventflags *_rtos2_alloc_eventflags(void);
extern __weak void _rtos2_release_eventflags(struct rtos_eventflags *eventflags);

__weak struct rtos_eventflags *_rtos2_alloc_eventflags(void)
{
	return _rtos2_alloc(sizeof(struct rtos_eventflags));
}

__weak void _rtos2_release_eventflags(struct rtos_eventflags *eventflags)
{
	_rtos2_release(eventflags);
}

osEventFlagsId_t osEventFlagsNew(const osEventFlagsAttr_t *attr)
{
	const osEventFlagsAttr_t default_attr = { .name = "" };

	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Check for attribute */
	if (!attr)
		attr = &default_attr;

	/* Setup the event flags memory and validate the size*/
	struct rtos_eventflags *new_eventflags = attr->cb_mem;
	if (!new_eventflags) {
		new_eventflags = _rtos2_alloc_eventflags();
		if (!new_eventflags)
			return 0;
	} else if (attr->cb_size < sizeof(struct rtos_eventflags))
		return 0;

	/* Initialize */
	new_eventflags->marker = RTOS_EVENTFLAGS_MARKER;
	strncpy(new_eventflags->name, (attr->name == 0 ? default_attr.name : attr->name), RTOS_NAME_SIZE);
	new_eventflags->name[RTOS_NAME_SIZE - 1] = 0;
	new_eventflags->attr_bits = attr->attr_bits | (new_eventflags != attr->cb_mem ? osDynamicAlloc : 0);
	new_eventflags->flags = 0;
	new_eventflags->waiters = 0;
	scheduler_futex_init(&new_eventflags->futex, (long *)&new_eventflags->flags, 0);
	list_init(&new_eventflags->resource_node);

	/* Add the new eventflags to the resource list */
	if (osKernelResourceAdd(osResourceEventFlags, &new_eventflags->resource_node) != osOK) {

		/* Only release dynamically allocation */
		if (new_eventflags->attr_bits & osDynamicAlloc)
			_rtos2_release_eventflags(new_eventflags);

		/* This only happen when the resource locking fails */
		return 0;
	}

	/* All good */
	return new_eventflags;
}

const char *osEventFlagsGetName(osEventFlagsId_t ef_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the event flags */
	os_status = osIsResourceValid(ef_id, RTOS_EVENTFLAGS_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_eventflags *eventflags= ef_id;

	/* Return the name */
	return strlen(eventflags->name) > 0 ? eventflags->name : 0;
}

uint32_t osEventFlagsSet(osEventFlagsId_t ef_id, uint32_t flags)
{
	/* Range check the input flags */
	if (flags & osFlagsError)
		return osFlagsErrorParameter;

	/* Validate the event flags */
	osStatus_t os_status = osIsResourceValid(ef_id, RTOS_EVENTFLAGS_MARKER);
	if (os_status != osOK)
		return osFlagsErrorParameter;
	struct rtos_eventflags *eventflags = ef_id;

	/* Run the algo */
	uint32_t prev_flags = atomic_fetch_or(&eventflags->flags, flags);
	if ((prev_flags & flags) != flags) {
		int status = scheduler_futex_wake(&eventflags->futex, true);
		if (status < 0)
			return osFlagsError;
		prev_flags |= flags;
	}

	/* Return the current flags */
	return prev_flags;
}

uint32_t osEventFlagsClear(osEventFlagsId_t ef_id, uint32_t flags)
{
	/* Range check the input flags */
	if (flags & osFlagsError)
		return osFlagsErrorParameter;

	/* Validate the event flags */
	osStatus_t os_status = osIsResourceValid(ef_id, RTOS_EVENTFLAGS_MARKER);
	if (os_status != osOK)
		return osFlagsErrorParameter;
	struct rtos_eventflags *eventflags = ef_id;

	/* Update the flags and return the current contents */
	return atomic_fetch_and(&eventflags->flags, ~flags);
}

uint32_t osEventFlagsGet(osEventFlagsId_t ef_id)
{
	/* Validate the event flags */
	osStatus_t os_status = osIsResourceValid(ef_id, RTOS_EVENTFLAGS_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_eventflags *eventflags = ef_id;

	/* Return the current flags */
	return eventflags->flags;
}

uint32_t osEventFlagsWait(osEventFlagsId_t ef_id, uint32_t flags, uint32_t options, uint32_t timeout)
{
	/* Range check the input flags */
	if (flags & osFlagsError)
		return osFlagsErrorParameter;

	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(true, timeout);
	if (os_status != osOK)
		return os_status;

	/* Validate the event flags */
	os_status = osIsResourceValid(ef_id, RTOS_EVENTFLAGS_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_eventflags *eventflags = ef_id;

	/* Track number of threads in inside the wait algo */
	++eventflags->waiters;

	/* Run the algo */
	uint32_t clear = (options & osFlagsNoClear) ? UINT32_MAX : ~flags;
	do {
		/* Get the current flags  */
		uint32_t prev_flags = atomic_load(&eventflags->flags);

		/* Are we done? */
		if (options & osFlagsWaitAll) {
			if ((prev_flags & flags) == flags) {
				--eventflags->waiters;
				return atomic_fetch_and(&eventflags->flags, clear);
			}
		} else {
			if ((prev_flags & flags) != 0) {
				--eventflags->waiters;
				return atomic_fetch_and(&eventflags->flags, clear);
			}
		}

		/* Try sematics? */
		if (timeout == 0) {
			--eventflags->waiters;
			return osFlagsErrorResource;
		}

		/* Force a thread exit with the high bit is set by osEventFlagsDelete */
		if (prev_flags & osFlagsError) {
			--eventflags->waiters;
			osThreadExit();
		}

		/* Nope wait for the flags */
		int status = scheduler_futex_wait(&eventflags->futex, prev_flags, timeout);
		if (status < 0) {
			--eventflags->waiters;
			return status == -ETIMEDOUT || status == -ECANCELED ? osErrorTimeout : osError;
		}

	} while (true);
}

osStatus_t osEventFlagsDelete(osEventFlagsId_t ef_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the eventflags */
	os_status = osIsResourceValid(ef_id, RTOS_EVENTFLAGS_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_eventflags *eventflags = ef_id;

	/* It is an error to have waiters */
	if (eventflags->waiters > 0)
		return osErrorResource;

	/* Remove the eventflags to the resource list */
	os_status = osKernelResourceRemove(osResourceEventFlags, &eventflags->resource_node);
	if (os_status != osOK)
		return os_status;

	/* Free the memory if the is dynamically allocated */
	if (eventflags->attr_bits & osDynamicAlloc)
		_rtos2_release_eventflags(eventflags);

	/* Yea, yea, done */
	return osOK;
}
