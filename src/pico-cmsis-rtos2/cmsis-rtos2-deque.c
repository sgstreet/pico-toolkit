/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * cmsis-rtos2-deque.c
 *
 *  Created on: Mar 24, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <stdlib.h>
#include <string.h>

#include <pico/toolkit/compiler.h>

#include <cmsis/cmsis-rtos2.h>

#define RTOS_DEQUE_SPACE_AVAILABLE 0x00000001UL
#define RTOS_DEQUE_DATA_AVAILABLE 0x00000002UL
#define RTOS_DEQUE_RESET 0x00000004UL

extern void *_rtos2_alloc(size_t size);
extern void _rtos2_release(void *ptr);

extern __weak struct rtos_deque *_rtos2_alloc_deque(size_t queue_size);
extern __weak void _rtos2_release_deque(struct rtos_deque *deque);

__weak struct rtos_deque *_rtos2_alloc_deque(size_t queue_size)
{
	return _rtos2_alloc(sizeof(struct rtos_deque) + queue_size);
}

__weak void _rtos2_release_deque(struct rtos_deque *deque)
{
	_rtos2_release(deque);
}

static inline unsigned int modulo(unsigned int x, unsigned int y)
{
	return x & (y - 1);
}

static inline size_t deque_inc(const struct rtos_deque *deque, size_t value)
{
	return modulo(value + 1, deque->element_count);
}

static inline size_t deque_dec(const struct rtos_deque *deque, size_t value)
{
	return modulo(value + 1, deque->element_count);
}

static inline void deque_get(const struct rtos_deque *deque, void *element, size_t pos)
{
	memcpy(element, deque->buffer + (pos * deque->element_size), deque->element_size);
}

static inline void deque_put(struct rtos_deque *deque, const void *element, size_t pos)
{
	memcpy(deque->buffer + (pos * deque->element_size), element, deque->element_size);
}

static inline bool deque_is_empty(const struct rtos_deque *deque)
{
	assert(deque != 0);
	return deque->front == deque->back;
}

static inline bool deque_is_full(const struct rtos_deque *deque)
{
	assert(deque != 0);
	return modulo(deque->back + 1, deque->element_count) == deque->front;
}

osDequeId_t osDequeNew(uint32_t element_count, uint32_t element_size, const osDequeAttr_t *attr)
{
	const osDequeAttr_t default_attr = { .name = "deque",  .attr_bits = 0 };

	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Check for attribute */
	if (!attr)
		attr = &default_attr;

	/* Are implementation require that the element count be a power of two */
	if (modulo(element_size, element_size) != 0)
		return 0;

	/* Setup the thread memory */
	struct rtos_deque *new_deque = 0;
	void *deque_data = 0;
	if (!attr->cb_mem && !attr->dq_mem) {

		/* Dynamic allocation */
		new_deque = _rtos2_alloc_deque(sizeof(struct rtos_deque) + (element_size * element_count));
		if (!new_deque)
			return 0;

		/* Initialize the pointers */
		new_deque->attr_bits = attr->attr_bits | osDynamicAlloc;
		deque_data = new_deque->data;

	/* Static allocation */
	} else if (attr->cb_mem && attr->dq_mem) {

		/* Make sure the stack size if reasonable */
		if (attr->cb_size < sizeof(struct rtos_deque) || attr->dq_size < sizeof(struct rtos_message) + (element_size * element_count))
			return 0;

		/* Initialize the pointers */
		new_deque = attr->cb_mem;
		deque_data = attr->dq_mem;

	/* Static memory allocation is all or nothing */
	} else
		return 0;

	/* Initialize the remaining parts of the queue  */
	new_deque->marker = RTOS_DEQUE_MARKER;
	strncpy(new_deque->name, (attr->name == 0 ? default_attr.name : attr->name), RTOS_NAME_SIZE);
	new_deque->name[RTOS_NAME_SIZE - 1] = 0;
	new_deque->element_size = element_size;
	new_deque->element_count = element_count;
	new_deque->front = 0;
	new_deque->back = 0;
	new_deque->buffer = deque_data;
	new_deque->lock = 0;

	/* Initialize the deque event flags */
	osEventFlagsAttr_t event_attr = { .name = attr->name, .cb_mem = &new_deque->events, .cb_size = sizeof(struct rtos_eventflags) };
	if (!osEventFlagsNew(&event_attr))
		goto delete_deque;

	/* Add to the kernel resources */
	if (osKernelResourceAdd(osResourceDeque, &new_deque->resource_node) != osOK)
		goto delete_eventflags;

	/* Yes it worked */
	return new_deque;

delete_eventflags:
	osEventFlagsDelete(&new_deque->events);

delete_deque:
	if (new_deque->attr_bits & osDynamicAlloc)
		_rtos2_release_deque(new_deque);

	/* Lots of work, no joy */
	return 0;
}

const char *osDequeGetName(osDequeId_t dq_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the deque */
	os_status = osIsResourceValid(dq_id, RTOS_DEQUE_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_deque *deque = dq_id;

	/* Return the name */
	return deque->name;
}

osStatus_t osDequePutFront(osDequeId_t dq_id, const void *element, uint32_t timeout)
{
	/* Make sure a message was provided */
	if (!element)
		return osErrorParameter;

	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(true, timeout);
	if (os_status != osOK)
		return os_status;

	/* Validate the message queue */
	os_status = osIsResourceValid(dq_id, RTOS_DEQUE_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_deque *deque = dq_id;

	/* Check for reset in progress */
	uint32_t flags = osEventFlagsGet(&deque->events);
	if (flags & (RTOS_DEQUE_RESET | osFlagsError)) {
		if (flags & RTOS_DEQUE_RESET)
			return osErrorResource;
		return (osStatus_t)flags;
	}

	/* We are a potential waiter */
	++deque->waiters;

	/* Wait for room */
	uint32_t state = spin_lock_irqsave(&deque->lock);
	while (deque_is_full(deque)) {

		/* Exit the critical section */
		spin_unlock_irqrestore(&deque->lock, state);

		/* Wait for room */
		flags = osEventFlagsWait(&deque->events, RTOS_DEQUE_SPACE_AVAILABLE | RTOS_DEQUE_RESET, osFlagsWaitAny | osFlagsNoClear, timeout);
		if (flags & osFlagsError) {
			--deque->waiters;
			return (osStatus_t)flags;
		}

		/* Clear the space available flag */
		flags = osEventFlagsClear(&deque->events, RTOS_DEQUE_SPACE_AVAILABLE);
		if (flags & osFlagsError) {
			--deque->waiters;
			return (osStatus_t)flags;
		}

		/* Is a reset in progress */
		if (flags & RTOS_DEQUE_RESET) {
			--deque->waiters;
			return osErrorResource;
		}

		/* Lock it up again */
		state = spin_lock_irqsave(&deque->lock);
	}

	/* Move the front of the queue first */
	deque->front = deque_dec(deque, deque->front - 1);

	/* Add to the back of the queue */
	deque_put(deque, element, deque->front);

	/* Exit the critical section */
	spin_unlock_irqrestore(&deque->lock, state);

	/* At last */
	--deque->waiters;

	/* Kick waiters for data, we are done */
	flags = osEventFlagsSet(&deque->events, RTOS_DEQUE_DATA_AVAILABLE);
	if (flags & osFlagsError)
		return (osStatus_t)flags;

	/* All good */
	return osOK;
}

osStatus_t osDequePutBack(osDequeId_t dq_id, const void *element, uint32_t timeout)
{
	/* Make sure a message was provided */
	if (!element)
		return osErrorParameter;

	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(true, timeout);
	if (os_status != osOK)
		return os_status;

	/* Validate the message queue */
	os_status = osIsResourceValid(dq_id, RTOS_DEQUE_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_deque *deque = dq_id;

	/* Check for reset in progress */
	uint32_t flags = osEventFlagsGet(&deque->events);
	if (flags & (RTOS_DEQUE_RESET | osFlagsError)) {
		if (flags & RTOS_DEQUE_RESET)
			return osErrorResource;
		return (osStatus_t)flags;
	}

	/* We are a potential waiter */
	++deque->waiters;

	/* Wait for room */
	uint32_t state = spin_lock_irqsave(&deque->lock);
	while (deque_is_full(deque)) {

		/* Exit the critical section */
		spin_unlock_irqrestore(&deque->lock, state);

		/* Wait for room */
		flags = osEventFlagsWait(&deque->events, RTOS_DEQUE_SPACE_AVAILABLE | RTOS_DEQUE_RESET, osFlagsWaitAny | osFlagsNoClear, timeout);
		if (flags & osFlagsError) {
			--deque->waiters;
			return (osStatus_t)flags;
		}

		/* Clear the space available flag */
		flags = osEventFlagsClear(&deque->events, RTOS_DEQUE_SPACE_AVAILABLE);
		if (flags & osFlagsError) {
			--deque->waiters;
			return (osStatus_t)flags;
		}

		/* Is a reset in progress */
		if (flags & RTOS_DEQUE_RESET) {
			--deque->waiters;
			return osErrorResource;
		}

		/* Lock it up again */
		state = spin_lock_irqsave(&deque->lock);
	}

	/* Add to the back of the queue */
	deque_put(deque, element, deque->back);

	/* Move the back of the queue */
	deque->back = deque_inc(deque, deque->back);

	/* Exit the critical section */
	spin_unlock_irqrestore(&deque->lock, state);

	/* At last */
	--deque->waiters;

	/* Kick waiters for data, we are done */
	flags = osEventFlagsSet(&deque->events, RTOS_DEQUE_DATA_AVAILABLE);
	if (flags & osFlagsError)
		return (osStatus_t)flags;

	/* All good */
	return osOK;
}

osStatus_t osDequeGetFront(osDequeId_t dq_id, void *element, uint32_t timeout)
{
	/* Make sure a message was provided */
	if (!element)
		return osErrorParameter;

	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(true, timeout);
	if (os_status != osOK)
		return os_status;

	/* Validate the message queue */
	os_status = osIsResourceValid(dq_id, RTOS_DEQUE_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_deque *deque = dq_id;

	/* Check for reset in progress */
	uint32_t flags = osEventFlagsGet(&deque->events);
	if (flags & (RTOS_DEQUE_RESET | osFlagsError)) {
		if (flags & RTOS_DEQUE_RESET)
			return osErrorResource;
		return (osStatus_t)flags;
	}

	/* We are a potential waiter */
	++deque->waiters;

	/* Wait for room */
	uint32_t state = spin_lock_irqsave(&deque->lock);
	while (deque_is_empty(deque)) {

		/* Exit the critical section */
		spin_unlock_irqrestore(&deque->lock, state);

		/* Wait for data */
		flags = osEventFlagsWait(&deque->events, RTOS_DEQUE_DATA_AVAILABLE | RTOS_DEQUE_RESET, osFlagsWaitAny | osFlagsNoClear, timeout);
		if (flags & osFlagsError) {
			--deque->waiters;
			return (osStatus_t)flags;
		}

		/* Clear the data available flag */
		flags = osEventFlagsClear(&deque->events, RTOS_DEQUE_DATA_AVAILABLE);
		if (flags & osFlagsError) {
			--deque->waiters;
			return (osStatus_t)flags;
		}

		/* Is a reset in progress */
		if (flags & RTOS_DEQUE_RESET) {
			--deque->waiters;
			return osErrorResource;
		}

		/* Lock it up again */
		state = spin_lock_irqsave(&deque->lock);
	}

	/* Get from the front of the queue */
	deque_get(deque, element, deque->front);

	/* Move the front the queue */
	deque->front = deque_inc(deque, deque->front);

	/* Exit the critical section */
	spin_unlock_irqrestore(&deque->lock, state);

	/* At last */
	--deque->waiters;

	/* Kick waiters for space */
	flags = osEventFlagsSet(&deque->events, RTOS_DEQUE_SPACE_AVAILABLE);
	if (flags & osFlagsError)
		return (osStatus_t)flags;

	/* All good */
	return osOK;
}

osStatus_t osDequeGetBack(osDequeId_t dq_id, void *element, uint32_t timeout)
{
	/* Make sure a message was provided */
	if (!element)
		return osErrorParameter;

	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(true, timeout);
	if (os_status != osOK)
		return os_status;

	/* Validate the message queue */
	os_status = osIsResourceValid(dq_id, RTOS_DEQUE_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_deque *deque = dq_id;

	/* Check for reset in progress */
	uint32_t flags = osEventFlagsGet(&deque->events);
	if (flags & (RTOS_DEQUE_RESET | osFlagsError)) {
		if (flags & RTOS_DEQUE_RESET)
			return osErrorResource;
		return (osStatus_t)flags;
	}

	/* We are a potential waiter */
	++deque->waiters;

	/* Wait for room */
	uint32_t state = spin_lock_irqsave(&deque->lock);
	while (deque_is_empty(deque)) {

		/* Exit the critical section */
		spin_unlock_irqrestore(&deque->lock, state);

		/* Wait for data */
		flags = osEventFlagsWait(&deque->events, RTOS_DEQUE_DATA_AVAILABLE | RTOS_DEQUE_RESET, osFlagsWaitAny | osFlagsNoClear, timeout);
		if (flags & osFlagsError) {
			--deque->waiters;
			return (osStatus_t)flags;
		}

		/* Clear the data available flag */
		flags = osEventFlagsClear(&deque->events, RTOS_DEQUE_DATA_AVAILABLE);
		if (flags & osFlagsError) {
			--deque->waiters;
			return (osStatus_t)flags;
		}

		/* Is a reset in progress */
		if (flags & RTOS_DEQUE_RESET) {
			--deque->waiters;
			return osErrorResource;
		}

		/* Lock it up again */
		state = spin_lock_irqsave(&deque->lock);
	}
	memcpy(element, deque->buffer + (deque->back * deque->element_size), deque->element_size);

	/* Move the back of the queue */
	deque->back = deque_dec(deque, deque->back);

	/* Get from the front of the queue */
	deque_get(deque, element, deque->back);

	/* Exit the critical section */
	spin_unlock_irqrestore(&deque->lock, state);

	/* At last */
	--deque->waiters;

	/* Kick waiters for space */
	flags = osEventFlagsSet(&deque->events, RTOS_DEQUE_SPACE_AVAILABLE);
	if (flags & osFlagsError)
		return (osStatus_t)flags;

	/* All good */
	return osOK;
}

uint32_t osDequeGetCapacity(osDequeId_t dq_id)
{
	/* Valid in interrupt context */
	osStatus_t os_status = osKernelContextIsValid(true, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the deque */
	os_status = osIsResourceValid(dq_id, RTOS_DEQUE_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_deque *deque = dq_id;

	/* Return the max space available */
	return deque->element_count;
}

uint32_t osDequeGetElementSize(osDequeId_t dq_id)
{
	/* Valid in interrupt context */
	osStatus_t os_status = osKernelContextIsValid(true, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the deque */
	os_status = osIsResourceValid(dq_id, RTOS_DEQUE_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_deque *deque = dq_id;

	/* Return the max space available */
	return deque->element_size;
}

uint32_t osDequeGetCount(osDequeId_t dq_id)
{
	/* Validate the deque */
	osStatus_t os_status = osIsResourceValid(dq_id, RTOS_DEQUE_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_deque *deque = dq_id;

	return modulo(deque->back - deque->front, deque->element_count);
}

uint32_t osDequeGetSpace(osDequeId_t dq_id)
{
	/* Validate the deque */
	osStatus_t os_status = osIsResourceValid(dq_id, RTOS_DEQUE_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_deque *deque = dq_id;

	return modulo(deque->front - (deque->back + 1), deque->element_count);
}

osStatus_t osDequeReset(osDequeId_t dq_id)
{
	/* Validate the context */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the deque */
	os_status = osIsResourceValid(dq_id, RTOS_DEQUE_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_deque *deque = dq_id;

	/* Flush all waiters */
	uint32_t flags = osEventFlagsSet(&deque->events, RTOS_DEQUE_RESET);
	if (flags & osFlagsError)
		return (osStatus_t)flags;

	/* Wait for everyone to leave */
	osPriority_t old_priority = osThreadGetPriority(osThreadGetId());
	osThreadSetPriority(osThreadGetId(), osPriorityIdle);
	while (deque->waiters)
		osThreadYield();
	osThreadSetPriority(osThreadGetId(), old_priority);

	/* Reset the queue */
	uint32_t state = spin_lock_irqsave(&deque->lock);
	deque->front = 0;
	deque->back = 0;
	spin_unlock_irqrestore(&deque->lock, state);

	/* Clear all flags */
	flags = osEventFlagsClear(&deque->events, RTOS_DEQUE_RESET | RTOS_DEQUE_DATA_AVAILABLE | RTOS_DEQUE_SPACE_AVAILABLE);
	if (flags & osFlagsError)
		return (osStatus_t)flags;

	/* Might be ok */
	return osOK;
}

osStatus_t osDequeDelete(osDequeId_t dq_id)
{
	/* Validate the context */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the deque */
	os_status = osIsResourceValid(dq_id, RTOS_DEQUE_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_deque *deque = dq_id;

	/* Clear the resource marker */
	deque->marker = 0;

	/* Remove the resource */
	os_status = osKernelResourceRemove(osResourceDeque, &deque->resource_node);
	if (os_status != osOK)
		return os_status;

	/* Release the event */
	os_status = osEventFlagsDelete(&deque->events);
	if (os_status != osOK)
		return os_status;

	/* Release any memory */
	if (deque->attr_bits & osDynamicAlloc)
		_rtos2_release_deque(deque);

	/* All good */
	return osOK;
}
