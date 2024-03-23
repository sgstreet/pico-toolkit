#include <stdlib.h>
#include <string.h>

#include <pico/toolkit/compiler.h>

#include <cmsis/cmsis-rtos2.h>

extern void *_rtos2_alloc(size_t size);
extern void _rtos2_release(void *ptr);

extern __weak struct rtos_timer *_rtos2_alloc_timer(void);
extern __weak void _rtos2_release_timer(struct rtos_timer *timer);

void scheduler_tick_hook(unsigned long ticks);

static osMessageQueueId_t timer_queue;
static osThreadId_t timer_thread;
static osOnceFlag_t timer_thread_init = osOnceFlagsInit;
static spinlock_t active_timers_lock = 0;
static struct linked_list active_timers = LIST_INIT(active_timers);

__weak struct rtos_timer *_rtos2_alloc_timer(void)
{
	return _rtos2_alloc(sizeof(struct rtos_timer));
}

__weak void _rtos2_release_timer(struct rtos_timer *timer)
{
	_rtos2_release(timer);
}

void scheduler_tick_hook(unsigned long ticks)
{
	/* Drop if not the first core in a multicore system */
	if (scheduler_current_core() != 0)
		return;

	/* Work to do? */
	while (!list_is_empty(&active_timers) && list_first_entry(&active_timers, struct rtos_timer, node)->target <= ticks) {

		/* Pop the expired timer */
		struct rtos_timer *expired = list_pop_entry(&active_timers, struct rtos_timer, node);

		/* Post to timer execution thread */
		osStatus_t os_status = osMessageQueuePut(timer_queue, &expired, 0, 0);
		if (os_status != osErrorResource && os_status != osOK)
			abort();

		/* If periodic, add back to the list */
		if (os_status == osErrorResource || expired->type == osTimerPeriodic) {

			/* Next target, or retry is dispatch queue is full */
			expired->target = ticks + (os_status == osErrorResource ? 0 : expired->ticks);

			/* Insert in order */
			struct rtos_timer *current;
			list_for_each_entry(current, &active_timers, node)
				if (expired->target < current->target)
					break;
			list_insert_before(&current->node, &expired->node);
		}

		/* Wait for next tick if timer worker thread is full */
		if (os_status == osErrorResource)
			break;
	}
}

static void osTimerThread(void *context)
{
	struct rtos_timer *timer;

	/* Allow kernel to exit even if we are still running */
	scheduler_set_flags(0, SCHEDULER_IGNORE_VIABLE);

	/* Run forever */
	while (true) {

		/* Wait for work */
		osStatus_t os_status = osMessageQueueGet(timer_queue, &timer, 0, osWaitForever);
		if (os_status != osOK)
			abort();

		/* Null timer pointer force exit */
		if (!timer)
			break;

		/* Dispatch */
		timer->func(timer->argument);
	}
}

static void osTimerThreadInit(osOnceFlagId_t flag_id, void *context)
{
	/* Create the time message queue */
	osMessageQueueAttr_t queue_attr = { .name = "osTimerQueue" };
	timer_queue = osMessageQueueNew(RTOS_TIMER_QUEUE_SIZE, sizeof(struct rtos_timer *), &queue_attr);
	if (!timer_queue)
		abort();

	/* Create the thread thread */
	osThreadAttr_t thread_attr = { .name = "osTimerThread", .stack_size = RTOS_DEFAULT_STACK_SIZE, .priority = osPriorityAboveNormal };
	timer_thread = osThreadNew(osTimerThread, 0, &thread_attr);
	if (!timer_thread)
		abort();
}

osTimerId_t osTimerNew (osTimerFunc_t func, osTimerType_t type, void *argument, const osTimerAttr_t *attr)
{
	const osTimerAttr_t default_attr = { .name = "" };

	/* Ensure the timer function is valid */
	if (!func)
		return 0;

	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Check for attribute */
	if (!attr)
		attr = &default_attr;

	/* Setup the timer memory and validate the size*/
	struct rtos_timer *new_timer = attr->cb_mem;
	if (!new_timer) {
		new_timer = _rtos2_alloc_timer();
		if (!new_timer)
			return 0;
	} else if (attr->cb_size < sizeof(struct rtos_timer))
		return 0;

	/* Initialize */
	new_timer->marker = RTOS_TIMER_MARKER;
	strncpy(new_timer->name, (attr->name == 0 ? default_attr.name : attr->name), RTOS_NAME_SIZE);
	new_timer->name[RTOS_NAME_SIZE - 1] = 0;
	new_timer->attr_bits = attr->attr_bits | (new_timer != attr->cb_mem ? osDynamicAlloc : 0);
	new_timer->type = type;
	new_timer->func = func;
	new_timer->argument = argument;
	list_init(&new_timer->resource_node);
	list_init(&new_timer->node);

	/* Add the new timer to the resource list */
	if (osKernelResourceAdd(osResourceTimer, &new_timer->resource_node) != osOK) {

		/* Only release dynamically allocation */
		if (new_timer->attr_bits & osDynamicAlloc)
			_rtos2_release_timer(new_timer);

		/* This only happen when the resource locking fails */
		return 0;
	}

	/* All good */
	return new_timer;
}

const char *osTimerGetName (osTimerId_t timer_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the timer */
	os_status = osIsResourceValid(timer_id, RTOS_TIMER_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_timer *timer = timer_id;

	/* Return the name */
	return strlen(timer->name) > 0 ? timer->name : 0;
}

osStatus_t osTimerStart (osTimerId_t timer_id, uint32_t ticks)
{
	/* Range check the ticks */
	if (ticks == 0 || ticks == osWaitForever)
		return osErrorParameter;

	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the timer */
	os_status = osIsResourceValid(timer_id, RTOS_TIMER_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_timer *timer = timer_id;

	/* Start the timer thread and initialize the message if needed */
	osCallOnce(&timer_thread_init, osTimerThreadInit, 0);

	/* Set the tick time */
	timer->ticks = ticks;
	timer->target = osKernelGetTickCount() + ticks;

	/* Add to the timer list */
	struct rtos_timer *current = 0;
	uint32_t state = spin_lock_irqsave(&active_timers_lock);
	list_for_each_entry(current, &active_timers, node)
		if (timer->target < current->target)
			break;
	list_insert_before(&current->node, &timer->node);
	spin_unlock_irqrestore(&active_timers_lock, state);

	/* All good */
	return osOK;
}

osStatus_t osTimerStop (osTimerId_t timer_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the timer */
	os_status = osIsResourceValid(timer_id, RTOS_TIMER_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_timer *timer = timer_id;

	/* Can only stop running timers */
	if (!list_is_linked(&timer->node))
		return osErrorResource;

	/* Remove timer */
	uint32_t state = spin_lock_irqsave(&active_timers_lock);
	list_remove(&timer->node);
	spin_unlock_irqrestore(&active_timers_lock, state);

	/* All good */
	return osOK;
}

uint32_t osTimerIsRunning (osTimerId_t timer_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the timer */
	os_status = osIsResourceValid(timer_id, RTOS_TIMER_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_timer *timer = timer_id;

	return list_is_linked(&timer->node);
}

osStatus_t osTimerDelete (osTimerId_t timer_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the timer */
	os_status = osIsResourceValid(timer_id, RTOS_TIMER_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_timer *timer = timer_id;

	/* Stop the timer if it is running */
	uint32_t state = spin_lock_irqsave(&active_timers_lock);
	if (list_is_linked(&timer->node))
		list_remove(&timer->node);
	spin_unlock_irqrestore(&active_timers_lock, state);

	/* Clear the marker */
	timer->marker = 0;

	/* Remove the timer to the resource list */
	os_status = osKernelResourceRemove(osResourceEventFlags, &timer->resource_node);
	if (os_status != osOK)
		return os_status;

	/* Free the memory if the is dynamically allocated */
	if (timer->attr_bits & osDynamicAlloc)
		_rtos2_release_timer(timer);

	/* Yea, yea, done */
	return osOK;
}
