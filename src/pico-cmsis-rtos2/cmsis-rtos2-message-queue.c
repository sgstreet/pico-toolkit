/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * cmsis-rtos2-message-queue.c
 *
 *  Created on: Mar 24, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <stdlib.h>
#include <string.h>

#include <pico/toolkit/compiler.h>

#include <cmsis/cmsis-rtos2.h>

extern void *_rtos2_alloc(size_t size);
extern void _rtos2_release(void *ptr);

extern __weak struct rtos_message_queue *_rtos2_alloc_message_queue(size_t queue_size);
extern __weak void _rtos2_release_message_queue(struct rtos_message_queue *message_queue);

__weak struct rtos_message_queue *_rtos2_alloc_message_queue(size_t queue_size)
{
	return _rtos2_alloc(sizeof(struct rtos_message_queue) + queue_size);
}

__weak void _rtos2_release_message_queue(struct rtos_message_queue *message_queue)
{
	_rtos2_release(message_queue);
}

osMessageQueueId_t osMessageQueueNew(uint32_t msg_count, uint32_t msg_size, const osMessageQueueAttr_t *attr)
{
	const osMessageQueueAttr_t default_attr = { .name = "" };

	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Check for attribute */
	if (!attr)
		attr = &default_attr;

	/* Round up the message size to word alignment */
	msg_size = (msg_size + 3) & ~3;

	/* Setup the thread memory */
	struct rtos_message_queue *new_queue = 0;
	void *pool_data = 0;
	size_t pool_size = 0;
	if (!attr->cb_mem && !attr->mq_mem) {

		/* Dynamic allocation */
		new_queue = _rtos2_alloc_message_queue((sizeof(struct rtos_message) + msg_size) * msg_count);
		if (!new_queue)
			return 0;

		/* Initialize the pointers */
		new_queue->attr_bits = attr->attr_bits | osDynamicAlloc;
		pool_data = new_queue->data;
		pool_size = (sizeof(struct rtos_message) + msg_size) * msg_count;

	/* Static allocation */
	} else if (attr->cb_mem && attr->mq_mem) {

		/* Make sure the size if reasonable */
		if (attr->cb_size < sizeof(struct rtos_message_queue) || attr->mq_size < (sizeof(struct rtos_message) + msg_size) * msg_count)
			return 0;

		/* Initialize the pointers */
		new_queue = attr->cb_mem;
		pool_data = attr->mq_mem;
		pool_size = attr->mq_size;

	/* Static memory allocation is all or nothing */
	} else
		return 0;

	/* Initialize the remaining parts of the queue  */
	new_queue->marker = RTOS_MESSAGE_QUEUE_MARKER;
	strncpy(new_queue->name, (attr->name == 0 ? default_attr.name : attr->name), RTOS_NAME_SIZE);
	new_queue->name[RTOS_NAME_SIZE - 1] = 0;
	new_queue->msg_size = msg_size;
	new_queue->msg_count = msg_count;
	list_init(&new_queue->messages);
	new_queue->lock = 0;

	/* Initialize the message pool */
	osMemoryPoolAttr_t pool_attr = { .name = new_queue->name, .cb_mem = &new_queue->message_pool, .cb_size = sizeof(struct rtos_memory_pool), .mp_mem = pool_data, .mp_size = pool_size };
	if (!osMemoryPoolNew(new_queue->msg_count, sizeof(struct rtos_message) + msg_size, &pool_attr))
		goto delete_queue;

	/* Initialize the message queue semaphore with zero messages */
	osSemaphoreAttr_t semaphore_attr = { .name = new_queue->name, .cb_mem = &new_queue->data_available, .cb_size = sizeof(struct rtos_semaphore) };
	if (!osSemaphoreNew(new_queue->msg_count, 0, &semaphore_attr))
		goto delete_pool;

	/* Add to the kernel resources */
	if (osKernelResourceAdd(osResourceMessageQueue, &new_queue->resource_node) != osOK)
		goto delete_semaphore;

	/* Yes it worked */
	return new_queue;

delete_semaphore:
	osSemaphoreDelete(&new_queue->data_available);

delete_pool:
	osMemoryPoolDelete(&new_queue->message_pool);

delete_queue:
	if (new_queue->attr_bits & osDynamicAlloc)
		_rtos2_release_message_queue(new_queue);

	/* Lots of work, no joy */
	return 0;
}

const char *osMessageQueueGetName(osMessageQueueId_t mq_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the message queue */
	os_status = osIsResourceValid(mq_id, RTOS_MESSAGE_QUEUE_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_message_queue *queue = mq_id;

	/* Return the name */
	return strlen(queue->name) > 0 ? queue->name : 0;
}

osStatus_t osMessageQueuePut(osMessageQueueId_t mq_id, const void *msg_ptr, uint8_t msg_prio, uint32_t timeout)
{
	/* Make sure a message was provided */
	if (!msg_ptr)
		return osErrorParameter;

	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(true, timeout);
	if (os_status != osOK)
		return os_status;

	/* Validate the message queue */
	os_status = osIsResourceValid(mq_id, RTOS_MESSAGE_QUEUE_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_message_queue *queue = mq_id;

	/* Allocate a message from the pool */
	struct rtos_message *message = osMemoryPoolAlloc(&queue->message_pool, timeout);
	if (!message)
		return timeout == 0 ? osErrorResource : osErrorTimeout;

	/* Load up the message */
	list_init(&message->node);
	message->priority = msg_prio;
	memcpy(message->data, msg_ptr, queue->msg_size);

	/* Insert in the correct priority position */
	uint32_t state = spin_lock_irqsave(&queue->lock);
	struct rtos_message *current;
	list_for_each_entry(current, &queue->messages, node)
		if (message->priority > current->priority)
			break;
	list_insert_before(&current->node, &message->node);
	spin_unlock_irqrestore(&queue->lock, state);

	/* Kick the semaphore */
	os_status = osSemaphoreRelease(&queue->data_available);
	if (os_status != osOK) {

		/* Remove the message */
		state = spin_lock_irqsave(&queue->lock);
		list_remove(&message->node);
		spin_unlock_irqrestore(&queue->lock, state);

		/* Ignore the pool error if any */
		osMemoryPoolFree(&queue->message_pool, message);
	}

	/* Maybe good */
	return os_status;
}

osStatus_t osMessageQueueGet(osMessageQueueId_t mq_id, void *msg_ptr, uint8_t *msg_prio, uint32_t timeout)
{
	/* Make sure a message was provided */
	if (!msg_ptr)
		return osErrorParameter;

	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(true, timeout);
	if (os_status != osOK)
		return os_status;

	/* Validate the message queue */
	os_status = osIsResourceValid(mq_id, RTOS_MESSAGE_QUEUE_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_message_queue *queue = mq_id;

	/* Acquire a message token */
	os_status = osSemaphoreAcquire(&queue->data_available, timeout);
	if (os_status != osOK)
		return timeout == 0 ? osErrorResource : osErrorTimeout;

	/* Remove the highest priority message */
	uint32_t state = spin_lock_irqsave(&queue->lock);
	struct rtos_message *message = list_pop_entry(&queue->messages, struct rtos_message, node);
	spin_unlock_irqrestore(&queue->lock, state);

	/* Was there a sync error? */
	if (!message)
		return osError;

	/* Hand back the message */
	memcpy(msg_ptr, message->data, queue->msg_size);
	if (msg_prio)
		*msg_prio = message->priority & 0xff;

	/* Release the message with much joy */
	return osMemoryPoolFree(&queue->message_pool, message);
}

uint32_t osMessageQueueGetCapacity(osMessageQueueId_t mq_id)
{
	/* Valid in interrupt context */
	osStatus_t os_status = osKernelContextIsValid(true, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the message queue */
	os_status = osIsResourceValid(mq_id, RTOS_MESSAGE_QUEUE_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_message_queue *queue = mq_id;

	/* Return the max space available */
	return queue->msg_count;
}

uint32_t osMessageQueueGetMsgSize(osMessageQueueId_t mq_id)
{
	/* Valid in interrupt context */
	osStatus_t os_status = osKernelContextIsValid(true, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the message queue */
	os_status = osIsResourceValid(mq_id, RTOS_MESSAGE_QUEUE_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_message_queue *queue = mq_id;

	/* Return the max space available */
	return queue->msg_size;
}

uint32_t osMessageQueueGetCount(osMessageQueueId_t mq_id)
{
	/* Validate the message queue */
	osStatus_t os_status = osIsResourceValid(mq_id, RTOS_MESSAGE_QUEUE_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_message_queue *queue = mq_id;

	return osSemaphoreGetCount(&queue->data_available);
}

uint32_t osMessageQueueGetSpace(osMessageQueueId_t mq_id)
{
	/* Validate the message queue */
	osStatus_t os_status = osIsResourceValid(mq_id, RTOS_MESSAGE_QUEUE_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_message_queue *queue = mq_id;

	return osMemoryPoolGetSpace(&queue->message_pool);
}

osStatus_t osMessageQueueReset(osMessageQueueId_t mq_id)
{
	/* Validate the context */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the message queue */
	os_status = osIsResourceValid(mq_id, RTOS_MESSAGE_QUEUE_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_message_queue *queue = mq_id;

	/* Release all queued messages */
	while (osSemaphoreGetCount(&queue->data_available) > 0) {

		/* Reduce the number of tokens */
		os_status = osSemaphoreAcquire(&queue->data_available, osWaitForever);
		if (os_status != osOK)
			return os_status;

		/* Pop a message */
		uint32_t state = spin_lock_irqsave(&queue->lock);
		struct rtos_message *message = list_pop_entry(&queue->messages, struct rtos_message, node);
		spin_unlock_irqrestore(&queue->lock, state);

		/* Was there a sync failure */
		if (!message)
			return osError;

		/* Return the message to the pool */
		os_status = osMemoryPoolFree(&queue->message_pool, message);
		if (os_status != osOK)
			return os_status;
	}

	/* Looks good */
	return osOK;
}

osStatus_t osMessageQueueDelete(osMessageQueueId_t mq_id)
{
	/* Validate the context */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the message queue */
	os_status = osIsResourceValid(mq_id, RTOS_MESSAGE_QUEUE_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_message_queue *queue = mq_id;

	/* Clear the resource marker */
	queue->marker = 0;

	/* Remove the resource */
	os_status = osKernelResourceRemove(osResourceMessageQueue, &queue->resource_node);
	if (os_status != osOK)
		return os_status;

	/* Release the semaphore */
	os_status = osSemaphoreDelete(&queue->data_available);
	if (os_status != osOK)
		return os_status;

	/* Release the memory pool */
	os_status = osMemoryPoolDelete(&queue->message_pool);
	if (os_status != osOK)
		return os_status;

	/* Release any memory */
	if (queue->attr_bits & osDynamicAlloc)
		_rtos2_release_message_queue(queue);

	/* All good */
	return osOK;
}
