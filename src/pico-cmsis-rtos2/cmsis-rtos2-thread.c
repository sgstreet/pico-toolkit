/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * cmsis-rtos2-thread.c
 *
 *  Created on: Mar 24, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <cmsis/cmsis-rtos2.h>

#define RTOS_REAPER_EXIT 0x00000001
#define RTOS_REAPER_CLEAN 0x00000002
#define RTOS_THREAD_JOINED 0x40000000

struct rtos2_thread_capture
{
	uint32_t count;
	uint32_t size;
	osThreadId_t *threads;
};

struct rtos2_robust_mutex_capture
{
	uint32_t count;
	uint32_t size;
	osThreadId_t thread;
	osMutexId_t *mutexes;
};

extern void *_rtos2_alloc(size_t size);
extern void _rtos2_release(void *ptr);

extern __weak struct rtos_thread *_rtos2_alloc_thread(size_t stack_size);
extern __weak void _rtos2_release_thread(struct rtos_thread *thread);
extern __weak void _rtos2_thread_stack_overflow(struct rtos_thread *thread);

extern void *__tls_size;

const size_t osThreadMinimumStackSize = sizeof(struct task) + (size_t)&__tls_size + sizeof(struct scheduler_frame) + 8;

static osThreadId_t reaper_thread = 0;
static osOnceFlag_t reaper_thread_init = osOnceFlagsInit;

__weak struct rtos_thread *_rtos2_alloc_thread(size_t stack_size)
{
	/* Calculate the required size needed to provide the request stack size and make it a multiple of 8 bytes */
	size_t thread_size = sizeof(struct rtos_thread) + stack_size;

	/* Do it */
	return _rtos2_alloc(thread_size);
}

__weak void _rtos2_release_thread(struct rtos_thread *thread)
{
	_rtos2_release(thread);
}

__weak void _rtos2_thread_stack_overflow(struct rtos_thread *thread)
{
	fprintf(stderr, "stack overflow: %s %p\n", thread->name, thread);
}

static osStatus_t osCaptureOwnedRobustMutexes(const osResource_t resource, void *context)
{
	/* Make sure we can work correctly */
	if (!context)
		return osError;

	/* Update the capture */
	struct rtos2_robust_mutex_capture *capture = context;
	if (capture->count < capture->size) {

		/* Capture the mutex */
		if (osMutexGetOwner(resource) == capture->thread)
			capture->mutexes[capture->count++] = resource;

		/* Continue */
		return osOK;
	}

	/* We are full */
	return true;
}

static osStatus_t osReleaseRobustMutex(osThreadId_t thread)
{
	osMutexId_t robust_mutexes[5];
	struct rtos2_robust_mutex_capture robust_capture = { .size = 5, .mutexes = robust_mutexes };

	/* Capture owns robust mutexes */
	robust_capture.thread = thread;
	do {
		/* Capture any robust mutexes owned by the thread */
		robust_capture.count = 0;
		osStatus_t os_status = osKernelResourceForEach(osResourceRobustMutex, osCaptureOwnedRobustMutexes, &robust_capture);
		if (os_status != osOK)
			abort();

		/* Now release the robust mutexes */
		for (size_t i = 0; i < robust_capture.count; ++i) {
			while (osMutexGetOwner(robust_capture.mutexes[i]) == thread) {
				os_status = osMutexRobustRelease(robust_capture.mutexes[i], thread);
				if (os_status != osOK)
					return os_status;
			}
		}

	} while (robust_capture.count == robust_capture.size);

	/* Done at last */
	return osOK;
}

static osStatus_t osThreadReap(const osResource_t resource, void *context)
{
	assert(context != 0);

	struct linked_list *reap_list = context;

	/* Maker the resource is valid */
	osStatus_t os_status = osIsResourceValid(resource, RTOS_THREAD_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_thread *thread = resource;

	/* Release inactive threads i.e. marked with a scheduler state of other */
	if (thread->attr_bits & osReapThread) {

		/* Remove from resource list */
		list_remove(&thread->resource_node);

		/* Add to the reap list */
		list_add(reap_list, &thread->resource_node);
	}

	/* All good */
	return osOK;
}

static void osThreadReaper(void *context)
{
	struct linked_list reap_list = LIST_INIT(reap_list);

	/* Allow kernel to exit even if we are still running */
	scheduler_set_flags(0, SCHEDULER_IGNORE_VIABLE);

	/* Cleanup until exit */
	while (true) {

		/* Wait for signal */
		uint32_t flags = osThreadFlagsWait(RTOS_REAPER_EXIT | RTOS_REAPER_CLEAN, osFlagsWaitAny, osWaitForever);
		if (flags & osFlagsError)
			abort();

		/* Reap some threads? If so do this in two stages because we do not want to hold a critical section while cleaning up */
		if (flags & RTOS_REAPER_CLEAN) {

			/* Clean until everything is clean */
			while (true) {

				/* Build reap list */
				osStatus_t os_status = osKernelResourceForEach(osResourceThread, osThreadReap, &reap_list);
				if (os_status != osOK)
					abort();

				/* We done is the last reap found no thread */
				if (list_is_empty(&reap_list))
					break;

				/* Clean up everything in the reap list, */
				struct rtos_thread *thread;
				while ((thread = list_pop_entry(&reap_list, struct rtos_thread, resource_node)) != 0) {

					/* Release any owned robust mutexes */
					os_status = osReleaseRobustMutex(thread);
					if (os_status != osOK)
						abort();

					/* Release the joiner event */
					os_status = osEventFlagsDelete(&thread->joiner);
					if (os_status != osOK)
						abort();

					/* Release the thread events */
					os_status = osEventFlagsDelete(&thread->flags);
					if (os_status != osOK)
						abort();

					/* Clear the marker */
					thread->marker = 0;

					/* Are we managing the memory? */
					if (thread->attr_bits & osDynamicAlloc)
						_rtos2_release_thread(thread);
				}
			}
		}

		/* Done? */
		if (flags & RTOS_REAPER_EXIT)
			break;
	}

	abort();
}

static void osThreadReaperInit(osOnceFlagId_t flag_id, void *context)
{
	/* Create the reaper thread */
	osThreadAttr_t attr = { .name = "osThreadReaper", .stack_size = RTOS_DEFAULT_STACK_SIZE, .priority = osPriorityNormal };
	reaper_thread = osThreadNew(osThreadReaper, 0, &attr);
	if (!reaper_thread)
		abort();
}

static void osSchedulerTaskEntryPoint(void *context)
{
	struct rtos_thread *rtos_thread = context;

	/* Validate the thread */
	assert(osIsResourceValid(rtos_thread, RTOS_THREAD_MARKER) == osOK);

	rtos_thread->func(rtos_thread->context);

	/* Forward to thread exit */
	osThreadExit();
}

static void osSchedulerTaskExitHandler(struct task *task)
{
	assert(task != 0);

	/* Extract the matching thread */
	assert(osIsResourceValid(task->context, RTOS_THREAD_MARKER) == osOK);
	struct rtos_thread *thread = task->context;

	/* Check for overflow */
	if (task->psp->r0 == (uint32_t)-EFAULT)
		_rtos2_thread_stack_overflow(thread);

	/* Joinable? */
	if ((thread->attr_bits & osThreadJoinable)) {

		/* Let any joiner know about this */
		uint32_t flags = osEventFlagsSet(&thread->joiner, RTOS_THREAD_JOINED);
		if (flags & osFlagsError)
			abort();

		/* The joiner will handle the cleanup */
		return;
	}

	/* Detached thread, mark for reaping and kick the reaper */
	thread->attr_bits |= osReapThread;
	uint32_t flags = osThreadFlagsSet(reaper_thread, RTOS_REAPER_CLEAN);
	if (flags & osFlagsError)
		abort();
}

osThreadId_t osThreadNew(osThreadFunc_t func, void *argument, const osThreadAttr_t *attr)
{
	const osThreadAttr_t default_attr = { .stack_size = RTOS_DEFAULT_STACK_SIZE, .priority = osPriorityNormal };

	/* Bail if not thread function provided */
	if (!func)
		return 0;

	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Check for attribute */
	if (!attr)
		attr = &default_attr;

	/* Range check the thread priority */
	if (attr->priority < osPriorityNone || attr->priority > osPriorityISR)
		return 0;

	/* Setup the thread memory */
	struct rtos_thread *new_thread = 0;
	size_t stack_size = 0;
	if (!attr->cb_mem && !attr->stack_mem) {

		/* We will need more room on the stack for book keeping */
		stack_size = osThreadMinimumStackSize + (attr->stack_size == 0 ? RTOS_DEFAULT_STACK_SIZE : attr->stack_size);

		/* Dynamic allocation */
		new_thread = _rtos2_alloc_thread(stack_size);
		if (!new_thread)
			return 0;

		/* Initialize the pointers */
		new_thread->stack = new_thread->stack_area;
		new_thread->stack_size = attr->stack_size == 0 ? RTOS_DEFAULT_STACK_SIZE : attr->stack_size;
		new_thread->attr_bits = attr->attr_bits | osDynamicAlloc;

	/* Static allocation */
	} else if (attr->cb_mem && attr->stack_mem) {

		/* Hopefully this will be enough room */
		stack_size = attr->stack_size;

		/* Make sure the stack size if reasonable */
		if (attr->cb_size < sizeof(struct rtos_thread) || stack_size < osThreadMinimumStackSize)
			return 0;

		/* Initialize the pointers */
		new_thread = attr->cb_mem;
		new_thread->stack = attr->stack_mem;
		new_thread->stack_size = attr->stack_size;
		new_thread->attr_bits = attr->attr_bits;

	/* Static memory allocation is all or nothing */
	} else
		return 0;

	/* Initialize the remain parts of the thread */
	strncpy(new_thread->name, (attr->name ? attr->name : ""), RTOS_NAME_SIZE - 1);
	new_thread->marker = RTOS_THREAD_MARKER;
	new_thread->func = func;
	new_thread->context = argument;
	list_init(&new_thread->resource_node);

	/* Initialized the the thread flags */
	osEventFlagsAttr_t eventflags_attr = { .name = attr->name, .cb_mem = &new_thread->flags, .cb_size = sizeof(struct rtos_eventflags) };
	if (!osEventFlagsNew(&eventflags_attr))
		goto delete_thread;

	/* Initialized the the joiner event flags */
	eventflags_attr.cb_mem = &new_thread->joiner;
	if (!osEventFlagsNew(&eventflags_attr))
		goto delete_flags;

	/* Assemble the scheduler task descriptor */
	struct task_descriptor desc;
	desc.entry_point = osSchedulerTaskEntryPoint;
	desc.exit_handler = osSchedulerTaskExitHandler;
	desc.context = new_thread;
	desc.flags = SCHEDULER_TASK_STACK_CHECK | ((attr->attr_bits & osThreadCreateSuspended) ? SCHEDULER_CREATE_SUSPENDED : 0);
	desc.priority = osSchedulerPriority(attr->priority == osPriorityNone ? osPriorityNormal : attr->priority);

	/* Add it to the kernel thread resource list */
	os_status = osKernelResourceAdd(osResourceThread, &new_thread->resource_node);
	if (os_status != osOK)
		goto delete_joiner;

	/* Launch the thread, the entry point handler will complete the initialization */
	if (!scheduler_create(new_thread->stack, stack_size, &desc))
		goto remove_resource;

	/* Joy */
	return new_thread;

remove_resource:
	osKernelResourceRemove(osResourceThread, &new_thread->resource_node);

delete_joiner:
	osEventFlagsDelete(&new_thread->joiner);

delete_flags:
	osEventFlagsDelete(&new_thread->flags);

delete_thread:
	if (new_thread->attr_bits & osDynamicAlloc)
		_rtos2_release_thread(new_thread);

	/* The big fail */
	return 0;
}

const char *osThreadGetName(osThreadId_t thread_id)
{
	/* Check the thread id is provided */
	struct rtos_thread *thread = (struct rtos_thread *)thread_id;
	if (!thread)
		return 0;

	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the thread */
	os_status = osIsResourceValid(thread_id, RTOS_THREAD_MARKER);
	if (os_status != osOK)
		return 0;


	/* Return the name */
	return thread->name[0] == 0 ? 0 : thread->name;
}

osThreadId_t osThreadGetId(void)
{
	osThreadId_t id = scheduler_task()->context;
	if (osIsResourceValid(id, RTOS_THREAD_MARKER) != osOK)
		return 0;
	return id;
}

osThreadState_t osThreadGetState(osThreadId_t thread_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return osThreadError;

	/* Validate the thread */
	os_status = osIsResourceValid(thread_id, RTOS_THREAD_MARKER);
	if (os_status != osOK)
		return osThreadError;

	struct rtos_thread *thread = thread_id;
	if (thread->attr_bits & osReapThread)
		return osThreadError;

	/* Remap the task states */
	switch (scheduler_get_state(thread->stack)) {

		case TASK_TERMINATED:
			return osThreadTerminated;

		case TASK_RUNNING:
			return osThreadRunning;

		case TASK_READY:
			return osThreadReady;

		case TASK_BLOCKED:
		case TASK_SUSPENDED:
			return osThreadBlocked;

		default:
			return osThreadError;
	}
}

uint32_t osThreadGetStackSize(osThreadId_t thread_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the thread */
	os_status = osIsResourceValid(thread_id, RTOS_THREAD_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_thread *thread = thread_id;

	/* Looks ok */
	return thread->stack_size;
}

uint32_t osThreadGetStackSpace(osThreadId_t thread_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the thread */
	os_status = osIsResourceValid(thread_id, RTOS_THREAD_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_thread *thread = thread_id;
	struct task *task = thread->stack;

	/* Lock the kernel so we the thread does not change */
	osKernelLock();
	uint32_t *current_pos = task->stack_marker;
	while (current_pos < task->stack_marker + (thread->stack_size / 4)) {
		if (*current_pos != SCHEDULER_STACK_MARKER)
			break;
		++current_pos;
	}
	osKernelUnlock();

	/* Return the unused stack */
	uint32_t unused_stack = (current_pos - task->stack_marker) * 4;
	return unused_stack;
}

osStatus_t osThreadSetPriority(osThreadId_t thread_id, osPriority_t priority)
{
	/* Range Check the priority */
	if (priority < osPriorityIdle || priority > osPriorityISR)
		return osErrorParameter;

	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return osErrorISR;

	/* Validate the thread */
	os_status = osIsResourceValid(thread_id, RTOS_THREAD_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_thread *thread = thread_id;

	/* Forward */
	int status = scheduler_set_priority(thread->stack, osSchedulerPriority(priority));
	if (status < 0)
		return osError;

	/* All done here */
	return osOK;
}

osPriority_t osThreadGetPriority(osThreadId_t thread_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return osPriorityError;

	/* Validate the thread */
	os_status = osIsResourceValid(thread_id, RTOS_THREAD_MARKER);
	if (os_status != osOK)
		return osPriorityError;
	struct rtos_thread *thread = thread_id;

	/* Return the mapped priority */
	return osKernelPriority(scheduler_get_priority(thread->stack));
}

osStatus_t osThreadYield(void)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the thread */
	os_status = osIsResourceValid(osThreadGetId(), RTOS_THREAD_MARKER);
	if (os_status != osOK)
		return os_status;

	/* Forward to the scheduler */
	scheduler_yield();

	/* All good, we got processor back */
	return osOK;
}

osStatus_t osThreadSuspend(osThreadId_t thread_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the thread */
	os_status = osIsResourceValid(thread_id, RTOS_THREAD_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_thread *thread = thread_id;

	/* Check the thread state */
	switch (osThreadGetState(thread_id)) {
		case osThreadReady:
		case osThreadRunning:
		case osThreadBlocked:
			break;

		default:
			return osErrorResource;
	}

	/* Can not suspend while locked, consider removing if needed, scheduler changes required but it can be made to work */
	if (thread_id == osThreadGetId() && rtos2_kernel->locked)
		return osError;

	/* Forward */
	int status = scheduler_suspend(thread->stack);
	if (status < 0)
		return osError;

	/* All good */
	return osOK;
}

osStatus_t osThreadResume(osThreadId_t thread_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the thread */
	os_status = osIsResourceValid(thread_id, RTOS_THREAD_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_thread *thread = thread_id;

	/* Check the thread state */
	if (osThreadGetState(thread_id) != osThreadBlocked)
		return osErrorResource;

	/* Forward */
	int status = scheduler_resume(thread->stack);
	if (status < 0)
		return osError;

	/* Moving on now */
	return osOK;
}

osStatus_t osThreadDetach(osThreadId_t thread_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the thread */
	os_status = osIsResourceValid(thread_id, RTOS_THREAD_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_thread *thread = thread_id;

	/* Make sure the thread is not already detached */
	if ((thread->attr_bits & osThreadJoinable) == 0)
		return osErrorResource;

	/* Ensure the reaper is running */
	osCallOnce(&reaper_thread_init, osThreadReaperInit, 0);

	/* Update */
	thread->attr_bits &= ~osThreadJoinable;
	if (osThreadGetState(thread_id) == osThreadTerminated) {
		thread->attr_bits |= osReapThread;
		uint32_t flags = osThreadFlagsSet(reaper_thread, RTOS_REAPER_CLEAN);
		if (flags & 0x80000000)
			return (osStatus_t)flags;
	}

	/* All good */
	return osOK;
}

void osThreadExit(void)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		abort();

	/* As well as this */
	osThreadId_t id = osThreadGetId();
	os_status = osIsResourceValid(id, RTOS_THREAD_MARKER);
	if (os_status != osOK)
		abort();
	struct rtos_thread *thread = id;

	/* Initialize the thread reaper if needed  */
	if ((thread->attr_bits & osThreadJoinable) == 0)
		osCallOnce(&reaper_thread_init, osThreadReaperInit, 0);

	/* Tell the scheduler to evict the task, cleaning happens on the reaper thread through the scheduler task exit handler callback for via osThreadJoin */
	scheduler_terminate(thread->stack);

	/* We should never get here but scheduler_terminate is not __no_return */
	abort();
}

osStatus_t osThreadJoin(osThreadId_t thread_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Sure the thread is know to the kernel */
	os_status = osKernelResourceIsRegistered(osResourceThread, thread_id);
	if (os_status == osErrorResource)
		return osErrorParameter;

	/* Validate the thread */
	os_status = osIsResourceValid(thread_id, RTOS_THREAD_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_thread *thread = thread_id;

	/* Check the state */
	if (osThreadGetState(thread_id) == osThreadError)
		return osErrorParameter;

	/* Must be joinable */
	if ((thread->attr_bits & osThreadJoinable) == 0)
		return osErrorResource;

	/* Make sure we are not trying to join ourselves */
	if (thread_id == osThreadGetId())
		return osErrorResource;

	/* Wait the the thread to terminate */
	uint32_t flags = osEventFlagsWait(&thread->joiner, RTOS_THREAD_JOINED, osFlagsWaitAny, osWaitForever);
	if (flags & osFlagsError)
		return (osStatus_t)flags;

	/* Remove the resource from the kernel */
	os_status = osKernelResourceRemove(osResourceThread, &thread->resource_node);
	if (os_status != osOK)
		return os_status;

	/* Release any robust mutexes owned by the thread */
	os_status = osReleaseRobustMutex(thread);
	if (os_status != osOK)
		return os_status;

	/* Release the joiner event */
	os_status = osEventFlagsDelete(&thread->joiner);
	if (os_status != osOK)
		return os_status;

	/* Release the thread events */
	os_status = osEventFlagsDelete(&thread->flags);
	if (os_status != osOK)
		return os_status;

	/* Clear the marker */
	thread->marker = 0;

	/* Are we managing the memory? */
	if (thread->attr_bits & osDynamicAlloc)
		_rtos2_release_thread(thread);

	/* We own the terminating thread clean up */
	return osOK;
}

osStatus_t osThreadTerminate(osThreadId_t thread_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the thread */
	os_status = osIsResourceValid(thread_id, RTOS_THREAD_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_thread *thread = thread_id;

	/* Initialize the thread reaper if needed */
	if ((thread->attr_bits & osThreadJoinable) == 0)
		osCallOnce(&reaper_thread_init, osThreadReaperInit, 0);

	/* Start the thread termination */
	int status = scheduler_terminate(thread->stack);
	if (status < 0) {

		/* Bad error? */
		if (status != -ESRCH)
			return osError;

		/* Check the state os the thread */
		osThreadState_t thread_state = osThreadGetState(thread_id);
		if (thread_state != osThreadTerminated && thread_state != osThreadError && status < 0)
			return osErrorResource;
	}

	/* Clean should be in progress */
	return osOK;
}

static osStatus_t osCountThreads(const osResource_t resource, void *context)
{
	if (!context)
		return osError;

	/* Only count active threads */
	osThreadState_t thread_state = osThreadGetState(resource);
	if (thread_state != osThreadError)
		*((int *)context) += 1;

	return osOK;
}

uint32_t osThreadGetCount(void)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Count them up */
	int count = 0;
	os_status = osKernelResourceForEach(osResourceThread, osCountThreads, &count);
	if (os_status != osOK)
		return 0;

	/* Done */
	return count;
}

static osStatus_t osCaptureThreads(const osResource_t resource, void *context)
{
	/* Make sure we can work correctly */
	if (!context)
		return osError;

	/* Update the capture */
	struct rtos2_thread_capture *capture = context;
	if (capture->count < capture->size && osThreadGetState(resource) != osThreadError)
		capture->threads[capture->count++] = resource;

	/* Continue */
	return osOK;
}

uint32_t osThreadEnumerate(osThreadId_t *thread_array, uint32_t array_items)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Enumerate */
	struct rtos2_thread_capture capture = { .count = 0, .size = array_items, .threads = thread_array };
	os_status = osKernelResourceForEach(osResourceThread, osCaptureThreads, &capture);
	if (os_status != osOK)
		return os_status;

	/* Number thread ids stored */
	return capture.count;
}

uint32_t osThreadFlagsSet(osThreadId_t thread_id, uint32_t flags)
{
	/* Range check the flags */
	if (flags & osFlagsError)
		return osErrorParameter;

	/* Validate the thread */
	osStatus_t os_status = osIsResourceValid(thread_id, RTOS_THREAD_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_thread *thread = thread_id;

	/* Forward */
	return osEventFlagsSet(&thread->flags, flags);
}

uint32_t osThreadFlagsClear(uint32_t flags)
{
	/* Spec is sort of broken as event flags can be cleared in a ISR but not thread flags */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Always clear the current thread flags */
	struct rtos_thread *thread = osThreadGetId();
	if (!thread)
		return osFlagsErrorUnknown;

	/* Forward */
	return osEventFlagsClear(&thread->flags, flags);
}

uint32_t osThreadFlagsGet(void)
{
	/* We always get the flags current thread */
	struct rtos_thread *thread = osThreadGetId();
	if (!thread)
		return osFlagsErrorUnknown;

	/* Forward */
	return osEventFlagsGet(&thread->flags);
}

uint32_t osThreadFlagsWait(uint32_t flags, uint32_t options, uint32_t timeout)
{
	/* Range check the flags */
	if (flags & osFlagsError)
		return osErrorParameter;

	/* Spec is sort of broken as event flags can be cleared in a ISR but not thread flags */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* We always wait on the current thread  */
	struct rtos_thread *thread = osThreadGetId();
	if (!thread)
		return osFlagsErrorUnknown;

	/* Forward */
	return osEventFlagsWait(&thread->flags, flags, options, timeout);
}
