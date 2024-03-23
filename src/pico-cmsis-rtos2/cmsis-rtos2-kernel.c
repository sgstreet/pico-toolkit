#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pico/toolkit/compiler.h>

#include <cmsis/cmsis-rtos2.h>

struct rtos_kernel *rtos2_kernel = 0;

extern __weak void *_rtos2_alloc(size_t size);
extern __weak void _rtos2_release(void *ptr);
extern void *__tls_size;

__weak void *_rtos2_alloc(size_t size)
{
	/* By default we use the monotonic allocator */
	void *ptr = sbrk(size);
	if (!ptr)
		return 0;

	/* Clear the allocation */

	return memset(ptr, 0, size);
}

__weak void _rtos2_release(void *ptr)
{
}

static struct rtos_kernel kernel = { .state = osKernelInactive };

osStatus_t osKernelInitialize(void)
{

	const char *resource_names[] =
	{
		"thread",
		"mutex",
		"robust_mutex",
		"memory_pool",
		"semaphore",
		"eventflags",
		"timer",
		"message_queue",
		"deque",
	};
	const size_t resource_offsets[] =
	{
		offsetof(struct rtos_thread, resource_node),
		offsetof(struct rtos_mutex, resource_node),
		offsetof(struct rtos_mutex, resource_node),
		offsetof(struct rtos_memory_pool, resource_node),
		offsetof(struct rtos_semaphore, resource_node),
		offsetof(struct rtos_eventflags, resource_node),
		offsetof(struct rtos_timer, resource_node),
		offsetof(struct rtos_message_queue, resource_node),
		offsetof(struct rtos_deque, resource_node)
	};
	const osResourceMarker_t resource_markers[] =
	{
		RTOS_THREAD_MARKER,
		RTOS_MUTEX_MARKER,
		RTOS_MUTEX_MARKER,
		RTOS_MEMORY_POOL_MARKER,
		RTOS_SEMAPHORE_MARKER,
		RTOS_EVENTFLAGS_MARKER,
		RTOS_TIMER_MARKER,
		RTOS_MESSAGE_QUEUE_MARKER,
		RTOS_DEQUE_MARKER,
	};

	/* Initialize the kernel lock */
	kernel.lock = 0;

	/* Can not be called from interrupt context */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Are we already initialized? */
	if (rtos2_kernel)
		return osError;

	/* Initialize the scheduler */
	int status = scheduler_init(&kernel.scheduler, (size_t)&__tls_size);
	if (status < 0)
		return osError;

	/* Initialize the resource lists, manually initialize lock to prevent initialization loops */
	for (osResourceId_t i = osResourceThread; i < osResourceLast; ++i) {

		/* Get the resource */
		struct rtos_resource *resource = &kernel.resources[i];

		/* Initialize basics stuff */
		resource->marker = resource_markers[i];
		strncpy(resource->name, resource_names[i], RTOS_NAME_SIZE);
		resource->name[RTOS_NAME_SIZE - 1] = 0;
		resource->offset = resource_offsets[i];
		list_init(&resource->resource_list);
		resource->lock = 0;
	}

	/* Mark the kernel as initialized */
	kernel.state = osKernelReady;
	rtos2_kernel = &kernel;

	/* All good */
	return osOK;
}

osStatus_t osKernelGetInfo(osVersion_t *version, char *id_buf, uint32_t id_size)
{
	if (version != 0) {
		version->api = 02001003;
		version->kernel = 02001003;
	}

	if (id_buf != 0) {
		snprintf(id_buf, id_size, "rtos-toolkit");
		id_buf[id_size - 1] = 0;
	}

	return osOK;
}

osKernelState_t osKernelGetState(void)
{
	/* Has the kernel been initialized? */
	if (!rtos2_kernel)
		return osKernelInactive;

	/* Return the reported state */
	return rtos2_kernel->state;
}

osStatus_t osKernelStart(void)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Can not start an uninitialized kernel */
	if (!rtos2_kernel || rtos2_kernel->state != osKernelReady)
		return osError;

	/* Start up the scheduler, we return here when the scheduler terminates */
	rtos2_kernel->state = osKernelRunning;
	int status = scheduler_run();

	/* Scheduler is down now */
	return status == 0 ? osOK : osError;
}

int32_t osKernelLock(void)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Kernel must be running or locked */
	if (rtos2_kernel->state != osKernelRunning && rtos2_kernel->state != osKernelLocked)
		return osError;

	/* Need a critical section to handle adaption */
	uint32_t state = spin_lock_irqsave(&rtos2_kernel->lock);
	int32_t prev_lock = rtos2_kernel->locked;
	rtos2_kernel->locked = true;
	rtos2_kernel->state = osKernelLocked;
	if (!prev_lock)
		scheduler_lock();
	spin_unlock_irqrestore(&rtos2_kernel->lock, state);

	/* Previous state */
	return prev_lock;
}

int32_t osKernelUnlock(void)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Kernel must be running or locked */
	if (rtos2_kernel->state != osKernelRunning && rtos2_kernel->state != osKernelLocked)
		return osError;

	/* Need a critical section to handle adaption */
	uint32_t state = spin_lock_irqsave(&rtos2_kernel->lock);
	int32_t prev_lock = rtos2_kernel->locked;
	rtos2_kernel->locked = false;
	rtos2_kernel->state = osKernelRunning;
	if (prev_lock)
		scheduler_unlock();
	spin_unlock_irqrestore(&rtos2_kernel->lock, state);

	/* Return the previous lock state */
	return prev_lock;
}

int32_t osKernelRestoreLock(int32_t lock)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Kernel must be running or locked */
	if (rtos2_kernel->state != osKernelRunning && rtos2_kernel->state != osKernelLocked)
		return osError;

	/* Need a critical section to handle adaption */
	uint32_t state = spin_lock_irqsave(&rtos2_kernel->lock);
	rtos2_kernel->locked = lock;
	if (lock) {
		scheduler_lock();
		rtos2_kernel->state = osKernelLocked;
	} else {
		scheduler_unlock();
		rtos2_kernel->state = osKernelRunning;
	}
	spin_unlock_irqrestore(&rtos2_kernel->lock, state);

	/* Return the previous lock state */
	return rtos2_kernel->locked;
}

uint32_t osKernelSuspend(void)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Bail if we are already suspended */
	if (rtos2_kernel->state == osKernelSuspended)
		return 0;

	/* Change the state */
	rtos2_kernel->state = osKernelSuspended;

	/* For now */
	return osWaitForever;
}

void osKernelResume(uint32_t sleep_ticks)
{
	/* This would be bad, drop the request */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return;

	rtos2_kernel->state = osKernelRunning;
}

uint32_t osKernelGetTickCount(void)
{
	return scheduler_get_ticks();
}

uint32_t osKernelGetTickFreq(void)
{
	return SCHEDULER_TICK_FREQ;
}

uint32_t osKernelGetSysTimerCount(void)
{
	uint32_t load = SysTick->LOAD;
	uint32_t sys_ticks = load - SysTick->VAL;
	return sys_ticks + osKernelGetTickCount() * (load + 1);
}

uint32_t osKernelGetSysTimerFreq(void)
{
	return SystemCoreClock;
}

void osCallOnce(osOnceFlagId_t once_flag, osOnceFunc_t func, void *context)
{
	/* All ready done */
	if (*once_flag == 2)
		return;

	/* Try to claim the initializer */
	int expected = 0;
	if (!atomic_compare_exchange_strong(once_flag, &expected, 1)) {

		/* Wait the the initializer to complete, we sleep to ensure lower priority threads run */
		while (*once_flag != 2)
			osDelay(10);

		/* Done */
		return;
	}

	/* Run the function */
	func(once_flag, context);

	/*  Mark as done */
	*once_flag = 2;
	__DSB();
}

osStatus_t osKernelResourceAdd(osResourceId_t resource_id, osResourceNode_t node)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Make a node was provided */
	if (!node)
		return osErrorParameter;

	/* Get the resource from the kernel */
	struct rtos_resource *resource = &rtos2_kernel->resources[resource_id];

	/* Lock the resource */
	uint32_t state = spin_lock_irqsave(&resource->lock);

	/* Add it */
	if (node == (void *)0x20005c80)
		list_init(node);

	list_add(&resource->resource_list, node);

	/* Release the resource */
	spin_unlock_irqrestore(&resource->lock, state);

	/* Done */
	return osOK;
}

osStatus_t osKernelResourceRemove(osResourceId_t resource_id, osResourceNode_t node)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Make sure a node was provided */
	if (!node)
		return osErrorParameter;

	/* Get the resource from the kernel */
	struct rtos_resource *resource = &rtos2_kernel->resources[resource_id];

	/* Lock the resource */
	uint32_t state = spin_lock_irqsave(&resource->lock);

	/* Remove it */
	list_remove(node);

	/* Release the resource */
	spin_unlock_irqrestore(&resource->lock, state);

	/* Kernel not running so everything is ok */
	return osOK;
}

osStatus_t osKernelResourceForEach(osResourceId_t resource_id, osResouceNodeForEachFunc_t func, void *context)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Make a node was provided */
	if (!func)
		return osErrorParameter;

	/* Get the resource from the kernel */
	struct rtos_resource *resource = &rtos2_kernel->resources[resource_id];

	/* Lock the resource */
	uint32_t state = spin_lock_irqsave(&resource->lock);

	/* Loop through all the node until either the end of the list or function return interesting status */
	osStatus_t func_status = osOK;
	struct linked_list *current;
	struct linked_list *next;
	list_for_each_mutable(current, next, &resource->resource_list) {
		osResource_t entry = ((void *)current) - resource->offset;
		func_status = func(entry, context);
		if (func_status != osOK)
			break;
	}

	/* Release the resource reporting any errors, if the kernel is running */
	/* Release the resource */
	spin_unlock_irqrestore(&resource->lock, state);

	/* Return the call back status */
	return func_status;
}

static osStatus_t osKernelDumpResource(osResource_t resource, void *context)
{
	/* Valid the resource */
	osResourceMarker_t marker = (osResourceMarker_t)context;
	osStatus_t os_status = osIsResourceValid(resource, marker);
	if (os_status != osOK)
		return os_status;

	/* Dump the resource */
	switch(marker) {

		case RTOS_THREAD_MARKER:
		{
			struct rtos_thread *thread = resource;
			fprintf(stdout, "thread: %p name: %s, state: %d stack available: %lu\n", thread, osThreadGetName(thread), osThreadGetState(thread), osThreadGetStackSpace(thread));
			break;
		}

		case RTOS_MUTEX_MARKER:
		case RTOS_MEMORY_POOL_MARKER:
		case RTOS_SEMAPHORE_MARKER:
		case RTOS_EVENTFLAGS_MARKER:
		case RTOS_TIMER_MARKER:
		case RTOS_MESSAGE_QUEUE_MARKER:
		case RTOS_DEQUE_MARKER:
		default:
			break;
	}

	/* Continue on */
	return osOK;
}

osStatus_t osKernelResourceDump(osResourceId_t resource_id)
{
	/* Check the kernel state */
	if (rtos2_kernel == 0 || rtos2_kernel->state != osKernelRunning)
		return osErrorResource;

	osResourceMarker_t marker = 0;
	switch (resource_id) {
		case osResourceThread:
			marker = RTOS_THREAD_MARKER;
			break;
		case osResourceMutex:
			marker = RTOS_MUTEX_MARKER;
			break;
		case osResourceMemoryPool:
			marker = RTOS_MEMORY_POOL_MARKER;
			break;
		case osResourceSemaphore:
			marker = RTOS_SEMAPHORE_MARKER;
			break;
		case osResourceEventFlags:
			marker = RTOS_EVENTFLAGS_MARKER;
			break;
		case osResourceTimer:
			marker = RTOS_TIMER_MARKER;
			break;
		case osResourceMessageQueue:
			marker = RTOS_MESSAGE_QUEUE_MARKER;
			break;
		case osResourceDeque:
			marker = RTOS_DEQUE_MARKER;
			break;
		default:
			return osErrorParameter;
	}

	/* Run the iterator */
	return osKernelResourceForEach(resource_id, osKernelDumpResource, (void *)marker);
}

static osStatus_t osKernelResourceReqistered(const osResource_t resource, void *context)
{
	if (resource == context)
		return true;
	return osOK;
}

osStatus_t osKernelResourceIsRegistered(osResourceId_t resource_id, osResource_t resource)
{
	int status = osKernelResourceForEach(resource_id, osKernelResourceReqistered, resource);
	if (status >= 1)
		return osOK;
	return osErrorResource;
}

error_t errno_from_rtos(int rtos)
{
	switch (rtos) {
		case osErrorTimeout:
			return ETIMEDOUT;
		case osErrorResource:
			return ERESOURCE;
		case osErrorParameter:
			return EINVAL;
		case osErrorNoMemory:
			return ENOMEM;
		case osErrorISR:
			return ENOTSUP;
		default:
			return ERTOS;
	}
}

