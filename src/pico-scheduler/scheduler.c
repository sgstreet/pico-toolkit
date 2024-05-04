/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Created on: Feb 13, 2017
 *     Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>

#include <pico/toolkit/cmsis.h>
#include <pico/toolkit/tls.h>
#include <pico/toolkit/scheduler.h>

#include "svc.h"

#define SCHEDULER_START_SVC 0
#define SCHEDULER_CREATE_SVC 1
#define SCHEDULER_YIELD_SVC 2
#define SCHEDULER_TERMINATE_SVC 3
#define SCHEDULER_SUSPEND_SVC 4
#define SCHEDULER_RESUME_SVC 5
#define SCHEDULER_WAIT_SVC 6
#define SCHEDULER_WAKE_SVC 7
#define SCHEDULER_PRIORITY_SVC 8

#define SCHEDULER_FRAME_NEEDED 0x00000002

#define ALIGNMENT_ROUND_SIZE(SIZE, BYTES) ((SIZE + (BYTES - 1)) & ~(BYTES - 1))
#define ALIGNMENT_ROUND_TYPE(TYPE, BYTES) ((sizeof(TYPE) + (BYTES - 1)) & ~(BYTES - 1))
#define DELAY_MAX (UINT32_MAX / 2)

#define sched_container_of(ptr, type, member) \
	({ \
        const typeof(((type *)0)->member) *__mptr = (ptr); \
        (type *)((char *)__mptr - offsetof(type, member)); \
	})

#define sched_container_of_or_null(ptr, type, member) \
	({ \
        const typeof(((type *)0)->member) *__mptr = (ptr); \
        __mptr ? (type *)((char *)__mptr - offsetof(type, member)) : 0; \
	})

void scheduler_start_svc(struct exception_frame *frame);
void scheduler_create_svc(struct exception_frame *frame);
void scheduler_yield_svc(struct exception_frame *frame);
void scheduler_terminate_svc(struct exception_frame *frame);
void scheduler_suspend_svc(struct scheduler_frame *frame);
void scheduler_resume_svc(struct exception_frame *frame);
void scheduler_wait_svc(struct scheduler_frame *frame);
void scheduler_wake_svc(struct exception_frame *frame);
void scheduler_priority_svc(struct exception_frame *frame);

struct scheduler_frame *scheduler_switch(struct scheduler_frame *frame);

extern __weak void scheduler_idle_hook(void);
extern __weak void scheduler_switch_hook(struct task *task);
extern __weak void scheduler_terminated_hook(struct task *task);
extern __weak void scheduler_tick_hook(unsigned long ticks);
extern __weak void scheduler_tls_init_hook(void *tls);
extern __weak void scheduler_startup_hook(void);
extern __weak void scheduler_shutdown_hook(void);

extern __weak void scheduler_spin_lock(void);
extern __weak void scheduler_spin_unlock(void);

extern __weak void enable_debugger_support(void);

uint32_t scheduler_svc_vector[] =
{
	(uint32_t) 0,
	(uint32_t) scheduler_create_svc,
	(uint32_t) scheduler_yield_svc,
	(uint32_t) scheduler_terminate_svc,
	(uint32_t) scheduler_suspend_svc,
	(uint32_t) scheduler_resume_svc,
	(uint32_t) scheduler_wait_svc,
	(uint32_t) scheduler_wake_svc,
	(uint32_t) scheduler_priority_svc,
};

struct scheduler *scheduler = 0;

core_local struct scheduler_frame *scheduler_initial_frame = 0;
core_local struct task *current_task = 0;
core_local int slice_expires = INT32_MAX;
core_local unsigned long ticks = 0;
core_local atomic_ulong deferred_wake[SCHEDULER_MAX_DEFERED_WAKE];
core_local atomic_ulong taken_wake_counter = 0;
core_local atomic_ulong given_wake_counter = 0;

static inline void sched_list_init(struct sched_list *list)
{
	list->next = list;
	list->prev = list;
}

static inline bool sched_list_empty(struct sched_list *list)
{
	assert(list != 0);

	return list->next == list;
}

static inline void sched_list_insert(struct sched_list *node, struct sched_list *first, struct sched_list *second)
{
	assert(node != 0 && first != 0 && second != 0);

	second->prev = node;
	node->next = second;
	node->prev = first;
	first->next = node;
}

static inline void sched_list_insert_after(struct sched_list *entry, struct sched_list *node)
{
	assert(entry != 0 && node != 0);

	node->next = entry->next;
	node->prev = entry;
	entry->next->prev = node;
	entry->next = node;
}

static inline void sched_list_insert_before(struct sched_list *entry, struct sched_list *node)
{
	assert(entry != 0 && node != 0);

	node->next = entry;
	node->prev = entry->prev;
	entry->prev->next = node;
	entry->prev = node;
}

static inline void sched_list_add(struct sched_list *list, struct sched_list *node)
{
	assert(list != 0 && node != 0);

	node->next = list;
	node->prev = list->prev;
	list->prev->next = node;
	list->prev = node;
}

static inline void sched_list_remove(struct sched_list *node)
{
	assert(node != 0);

	node->next->prev = node->prev;
	node->prev->next = node->next;
	node->next = node;
	node->prev = node;
}

static inline void sched_list_push(struct sched_list *list, struct sched_list *node)
{
	assert(list != 0 && node != 0);

	sched_list_insert(node, list->prev, list);
}

static inline struct sched_list *sched_list_pop(struct sched_list *list)
{
	assert(list != 0);

	struct sched_list *node = list->next;

	if (node == list)
		return 0;

	sched_list_remove(node);

	return node;
}

static inline int sched_list_is_linked(struct sched_list *node)
{
	assert(node != 0);

	return node->next != node;
}

#define sched_list_pop_entry(list, type, member) sched_container_of_or_null(sched_list_pop(list), type, member)
#define sched_list_entry(ptr, type, member) sched_container_of_or_null(ptr, type, member)
#define sched_list_first_entry(list, type, member) sched_list_entry((list)->next, type, member)
#define sched_list_last_entry(list, type, member) sched_list_entry((list)->prev, type, member)
#define sched_list_next_entry(position, member) sched_list_entry((position)->member.next, typeof(*(position)), member)
#define sched_list_prev_entry(position, member) sched_list_entry((position)->member.prev, typeof(*(position)), member)

#define sched_list_for_each(cursor, list) \
	for (cursor = (list)->next; cursor != (list); cursor = cursor->next)

#define sched_list_for_each_mutable(cursor, current, list) \
	for (cursor = (list)->next, current = cursor->next; cursor != (list); cursor = current, current = cursor->next)

#define sched_list_for_each_entry(cursor, list, member) \
	for (cursor = sched_list_first_entry(list, typeof(*cursor), member); &cursor->member != (list); cursor = sched_list_next_entry(cursor, member))

#define sched_list_for_each_entry_mutable(cursor, current, list, member) \
	for (cursor = sched_list_first_entry(list, typeof(*cursor), member), current = sched_list_next_entry(cursor, member); &cursor->member != (list); cursor = current, current = sched_list_next_entry(current, member))

#if 0
#include <diag/diag.h>
static __unused void sched_dump_queue(struct sched_queue *queue)
{
	struct task *entry;
	sched_list_for_each_entry(entry, &queue->tasks, queue_node) {
		assert(entry->marker == SCHEDULER_TASK_MARKER);
		diag_printf("%p - %s\n", entry, entry->name);
	}
}
#endif

static inline struct task *sched_get_current(void)
{
	struct task *task = cls_datum(current_task);
	assert(task == 0 || task->marker == SCHEDULER_TASK_MARKER);
	return task;
}

static inline struct task *sched_set_current(struct task *task)
{
	assert(task == 0 || task->marker == SCHEDULER_TASK_MARKER);

	struct task *prev = cls_datum(current_task);
	cls_datum(current_task) = task;

	scheduler_switch_hook(task);

	return prev;
}

static inline void sched_queue_init(struct sched_queue *queue)
{
	assert(queue != 0);
	sched_list_init(&queue->tasks);
}

static inline bool sched_queue_empty(struct sched_queue *queue)
{
	assert(queue != 0);
	return sched_list_empty(&queue->tasks);
}

static inline void sched_queue_remove(struct task *task)
{
	assert(task != 0);

	sched_list_remove(&task->queue_node);
	task->current_queue = 0;
}

static void sched_queue_push(struct sched_queue *queue, struct task *task)
{
	assert(queue != 0 && task != 0 && task->current_queue == 0);

	/* Find the insert point */
	struct task *entry = 0;
	struct sched_list *node;
	sched_list_for_each(node, &queue->tasks)
		if (sched_container_of(node, struct task, queue_node)->current_priority > task->current_priority) {
			entry = sched_container_of(node, struct task, queue_node);;
			break;
		}

	/* Insert at the correct position, which might be the head */
	if (entry)
		sched_list_insert_before(&entry->queue_node, &task->queue_node);
	else
		sched_list_push(&queue->tasks, &task->queue_node);

	task->current_queue = queue;
}

static struct task *sched_queue_pop(struct sched_queue *queue, unsigned long core)
{
	struct task *task;

	assert(queue != 0);

	/* Just take the first task */
	if (core == UINT32_MAX) {
		task = sched_list_pop_entry(&queue->tasks, struct task, queue_node);
		if (task)
			task->current_queue = 0;
		return task;
	}

	/* Look for the highest priority task which can run on this core */
	sched_list_for_each_entry(task, &queue->tasks, queue_node) {
		if ((task->flags & SCHEDULER_CORE_AFFINITY) == 0 || task->affinity == core) {
			sched_queue_remove(task);
			return task;
		}
	}

	return 0;
}

static inline unsigned long sched_queue_highest_priority(struct sched_queue *queue)
{
	unsigned long highest = SCHEDULER_NUM_TASK_PRIORITIES;

	assert(queue != 0);

	if (!sched_queue_empty(queue))
		highest = sched_list_first_entry(&queue->tasks, struct task, queue_node)->current_priority;

	return highest;
}

static void sched_queue_reprioritize(struct task *task, unsigned long new_priority)
{
	assert(task != 0 && new_priority >= 0 && new_priority < SCHEDULER_NUM_TASK_PRIORITIES);

	task->current_priority = new_priority;
	struct sched_queue *queue = task->current_queue;
	if (queue) {
		sched_queue_remove(task);
		sched_queue_push(queue, task);
	}
}

static inline __always_inline bool is_interrupt_context(void)
{
	return __get_IPSR() != 0;
}

static inline __always_inline bool is_svc_context(void)
{
	uint32_t result;

	asm volatile ("mrs %0, ipsr" : "=r" (result));

	return result == 11;
}

static inline __always_inline bool scheduler_check_stack(struct task *task)
{
	return ((task->flags & SCHEDULER_TASK_STACK_CHECK) == 0) || (task->stack_marker[0] == SCHEDULER_STACK_MARKER && task->stack_marker[1] == SCHEDULER_STACK_MARKER);
}

static void scheduler_timer_push(struct task *task, uint32_t delay)
{
	assert(task != 0);

	/* Remove any existing timers */
	sched_list_remove(&task->timer_node);

	/* Initialize the timer */
	task->timer_expires = scheduler_get_ticks() + delay;

	/* Find the insert point */
	struct task *entry;
	sched_list_for_each_entry(entry, &scheduler->timers, timer_node)
		if (entry->timer_expires > task->timer_expires)
			break;

	/* Insert at the correct position, which might be the head */
	sched_list_insert_before(&entry->timer_node, &task->timer_node);

	/* Update the new timer expire */
	unsigned long closest = sched_list_first_entry(&scheduler->timers, struct task, timer_node)->timer_expires;

	/* Save it */
	scheduler->timer_expires = closest;
}

static void scheduler_timer_remove(struct task *task)
{
	assert(task != 0);

	sched_list_remove(&task->timer_node);

	/* Update the new timer expire */
	unsigned long closest = sched_list_empty(&scheduler->timers) ? UINT_MAX : sched_list_first_entry(&scheduler->timers, struct task, timer_node)->timer_expires;

	/* Save it */
	scheduler->timer_expires = closest;
}

static struct task *scheduler_timer_pop(void)
{
	struct task *task = 0;

	/* Is the timer list empty? */
	if (!sched_list_empty(&scheduler->timers)) {

		/* Check for expired timer */
		task = sched_list_first_entry(&scheduler->timers, struct task, timer_node);
		if (task->timer_expires <= scheduler_get_ticks())
			scheduler_timer_remove(task);
		else
			task = 0;
	}

	/* No expired timers */
	return task;
}

__weak unsigned long scheduler_get_ticks(void)
{
	/* By default we use the core 0 ticks as the reference */
	return cls_datum_core(0, ticks);
}

__fast_section __optimize void scheduler_tick(void)
{
	/* Someone may have enabled us too early, ignore */
	if (!scheduler_is_running())
		return;

	/* Update the core tick count */
	++cls_datum(ticks);

	/* Get data for tick handling, we use the API to allow a single tick truth */
	unsigned long timer_expires = scheduler->timer_expires;
	unsigned long ticks = scheduler_get_ticks();

	/* Check for expired timer */
	if (timer_expires <= ticks)
		scheduler_request_switch(scheduler_current_core());

	/* And time slice enabled and expired */
	if (cls_datum(slice_expires) != INT32_MAX && --cls_datum(slice_expires) == 0)
		scheduler_request_switch(scheduler_current_core());

	/* Pass to the hook */
	scheduler_tick_hook(ticks);
}

void scheduler_create_svc(struct exception_frame *frame)
{
	assert(frame->r0 != 0 && scheduler != 0);

	struct task *task = (struct task *)frame->r0;

	scheduler_spin_lock();

	assert(task->marker == SCHEDULER_TASK_MARKER);

	/* Add the task the scheduler list */
	sched_list_push(&scheduler->tasks, &task->scheduler_node);

	/* Add the new task to the ready queue */
	if ((task->flags & SCHEDULER_CREATE_SUSPENDED) == 0) {

		/* Ready the task */
		task->state = TASK_READY;
		sched_queue_push(&scheduler->ready_queue, task);

		/* Since we pushed the task onto the ready queue, do a context switch and return the new task */
		if (scheduler_is_running() && task->current_priority < sched_get_current()->current_priority)
			scheduler_request_switch(scheduler_current_core());

	} else
		/* Mark as suspended */
		task->state = TASK_SUSPENDED;


	scheduler_spin_unlock();
}

void scheduler_yield_svc(struct exception_frame *frame)
{
	/* Pend the context switch to switch to the next task */
	scheduler_request_switch(scheduler_current_core());
}

static int scheduler_task_alive(const struct task *task)
{
	if (task != 0) {
		struct sched_list *current = 0;
		sched_list_for_each(current , &scheduler->tasks)
			if (&task->scheduler_node == current)
				return 0;
	}

	/* Not alive */
	return -ESRCH;
}

void scheduler_suspend_svc(struct scheduler_frame *frame)
{
	struct task *current = sched_get_current();
	struct task *task = (struct task *)frame->r0;
	unsigned long ticks = frame->r1;

	/* Close the dog house door */
	scheduler_spin_lock();

	/* Make sure the task is alive */
	frame->r0 = scheduler_task_alive(task);
	if (frame->r0 != 0) {
		scheduler_spin_unlock();
		return;
	}

	/* Who are we suspending */
	if (task != current) {

		/* Remove task from any blocked queues and timeouts */
		sched_queue_remove(task);
		scheduler_timer_remove(task);

		/* Mark as suspended */
		task->state = TASK_SUSPENDED;
		task->core = UINT32_MAX;

		/* Since a scheduler frame was create we always need a context switch */
		current->state = TASK_READY;
		sched_queue_push(&scheduler->ready_queue, current);

	} else {

		/* Suspending ourselves, add to the suspend queue */
		current->state = TASK_SUSPENDED;
		current->core = UINT32_MAX;
	}

	/* Add any need timer */
	if (ticks < SCHEDULER_WAIT_FOREVER)
		scheduler_timer_push(task, ticks);

	/* We need a context switch */
	current->psp = frame;
	sched_set_current(0);
	scheduler_request_switch(scheduler_current_core());

	/* Unleash the dogs */
	scheduler_spin_unlock();
}

void scheduler_resume_svc(struct exception_frame *frame)
{
	struct task *task = (struct task *)frame->r0;

	/* We should be could */
	frame->r0 = 0;

	scheduler_spin_lock();

	/* Make sure the task is alive */
	frame->r0 = scheduler_task_alive(task);
	if (frame->r0 != 0) {
		scheduler_spin_unlock();
		return;
	}

	assert(task != 0 && task->marker == SCHEDULER_TASK_MARKER);

	/* Wake up suspended, sleeping and blocked tasks only */
	enum task_state state = task->state;
	if (state == TASK_BLOCKED || state == TASK_SUSPENDED) {

		/* Might have an associated timer, remove */
		scheduler_timer_remove(task);

		/* Remove any blocking queue */
		sched_queue_remove(task);

		/* Waiting tasks return -ECANCELED when the wait is broken via resume */
		if (task->state == TASK_BLOCKED)
			task->psp->r0 = -ECANCELED;

		/* Push on the ready queue */
		task->state = TASK_READY;
		sched_queue_push(&scheduler->ready_queue, task);

	} else
		frame->r0 = -EINVAL;

	/* Request a context switch */
	scheduler_request_switch(scheduler_current_core());
	scheduler_spin_unlock();
}

void scheduler_wait_svc(struct scheduler_frame *frame)
{
	struct futex *futex = (struct futex *)frame->r0;
	long expected = (long)frame->r1;
	long value = (futex->flags & SCHEDULER_FUTEX_CONTENTION_TRACKING) ? expected | SCHEDULER_FUTEX_CONTENTION_TRACKING : expected;
	unsigned long ticks = frame->r2;
	struct task *current = sched_get_current();

	scheduler_spin_lock();

	assert(futex != 0 && futex->marker == SCHEDULER_FUTEX_MARKER && current != 0);

	/* At this point assume no timeout */
	frame->r0 = 0;

	/* Should we block? The second clause prevents a wakeup when the futex becomes contended while on the way into the wait */
	if (atomic_compare_exchange_strong(futex->value, &expected, value) || expected == value) {

		/* Add a timeout if requested */
		if (ticks < SCHEDULER_WAIT_FOREVER)
			scheduler_timer_push(current, ticks);

		/* Add to the waiter queue */
		current->state = TASK_BLOCKED;
		current->core = UINT32_MAX;
		sched_queue_push(&futex->waiters, current);

		/* Was priority inheritance requested */
		if ((futex->flags & (SCHEDULER_FUTEX_PI | SCHEDULER_FUTEX_OWNER_TRACKING)) == (SCHEDULER_FUTEX_PI | SCHEDULER_FUTEX_OWNER_TRACKING)) {

			/* Extract the owning task */
			struct task *owner = (struct task *)(value & ~SCHEDULER_FUTEX_CONTENTION_TRACKING);
			assert(owner->marker == SCHEDULER_TASK_MARKER);

			/* Add the futex to the list of owned, contented futexes */
			if (!sched_list_is_linked(&futex->owned))
				sched_list_add(&owner->owned_futexes, &futex->owned);

			/* Do we need to boost the priority of the futex owner? */
			unsigned long highest_priority = sched_queue_highest_priority(&futex->waiters);
			if (highest_priority < owner->current_priority)
				sched_queue_reprioritize(owner, highest_priority);
		}

	} else {

		/* Futex already triggered, we will need a need to complete for the processor */
		current->state = TASK_READY;
		sched_queue_push(&scheduler->ready_queue, current);
	}


	/* Always perform a context switch  */
	current->psp = frame;
	sched_set_current(0);
	scheduler_request_switch(scheduler_current_core());

	/* The dogs are loose */
	scheduler_spin_unlock();
}

static int scheduler_wake_futex(struct futex *futex, bool all)
{
	int woken = 0;

	/* If a PI futex, adjust priority current owner */
	if ((futex->flags & (SCHEDULER_FUTEX_PI | SCHEDULER_FUTEX_OWNER_TRACKING)) == (SCHEDULER_FUTEX_PI | SCHEDULER_FUTEX_OWNER_TRACKING)) {

		/* Extract the owning task */
		struct task *owner = (struct task *)(*futex->value & ~SCHEDULER_FUTEX_CONTENTION_TRACKING);
		assert(owner->marker == SCHEDULER_TASK_MARKER);

		/* Remove the this futex from the owned list */
		sched_list_remove(&futex->owned);

		/* Now find the highest priority of remaining owned PI futexes */
		unsigned long highest_priority = owner->base_priority;
		struct futex *owned;
		sched_list_for_each_entry(owned, &owner->owned_futexes, owned) {
			unsigned long highest_waiter = sched_queue_highest_priority(&owned->waiters);
			if (highest_waiter < highest_priority)
				highest_priority = highest_waiter;
		}

		/* Re-prioritize the task */
		sched_queue_reprioritize(owner, highest_priority);
	}

	/* Wake up the waiters */
	struct task *task;
	while ((task = sched_queue_pop(&futex->waiters,UINT32_MAX)) != 0) {

		assert(task->marker == SCHEDULER_TASK_MARKER);

		if (futex->flags & SCHEDULER_FUTEX_OWNER_TRACKING)
			atomic_exchange(futex->value, (long)task);

		/* Was priority inheritance requested */
		if ((futex->flags & SCHEDULER_FUTEX_PI) && !sched_queue_empty(&futex->waiters)) {

			/* Add the futex to the list of owned, contented futexes */
			sched_list_add(&task->owned_futexes, &futex->owned);

			/* Adjust the priority of the new owner */
			sched_queue_reprioritize(task, sched_queue_highest_priority(&futex->waiters));
		}

		/* Adjust queue */
		scheduler_timer_remove(task);
		task->state = TASK_READY;
		sched_queue_push(&scheduler->ready_queue, task);

		/* Continue waking more tasks? */
		++woken;
		if (!all || (futex->flags & SCHEDULER_FUTEX_OWNER_TRACKING))
			break;
	}

	/* Update the contention tracking if requested */
	if (futex->flags & SCHEDULER_FUTEX_CONTENTION_TRACKING) {
		if (sched_queue_empty(&futex->waiters))
			atomic_fetch_and(futex->value, ~SCHEDULER_FUTEX_CONTENTION_TRACKING);
		else
			atomic_fetch_or(futex->value, SCHEDULER_FUTEX_CONTENTION_TRACKING);
	}

	/* Return the number of woken tasks */
	return woken;
}

void scheduler_wake_svc(struct exception_frame *frame)
{
	struct futex *futex = (struct futex *)frame->r0;
	bool all = (bool)frame->r1;

	/* All dogs in there cages */
	scheduler_spin_lock();

	assert(futex != 0 && futex->marker == SCHEDULER_FUTEX_MARKER);

	/* Run wake algo */
	frame->r0 = scheduler_wake_futex(futex, all);

	/* Request a context switch if we woke anyone */
	if (frame->r0 > 0)
		scheduler_request_switch(scheduler_current_core());

	/* Let the fur fly */
	scheduler_spin_unlock();
}

void scheduler_terminate_svc(struct exception_frame *frame)
{
	struct task *current = sched_get_current();
	struct task *task = (struct task *)frame->r0;

	scheduler_spin_lock();

	/* Make sure the task is alive */
	frame->r0 = scheduler_task_alive(task);
	if (frame->r0 != 0) {
		scheduler_spin_unlock();
		return;
	}

	/* Clean up the task */
	task->state = TASK_TERMINATED;
	task->core = UINT32_MAX;
	sched_queue_remove(task);
	scheduler_timer_remove(task);
	sched_list_remove(&task->scheduler_node);

	/* Forward to the termination handler */
	scheduler_terminated_hook(task);

	/* if we are terminating ourselves we need a context switch */
	if (task == current) {
		sched_set_current(0);
		scheduler_request_switch(scheduler_current_core());
	}

	/* Release the block */
	scheduler_spin_unlock();
}

void scheduler_priority_svc(struct exception_frame *frame)
{
	struct task *task = (struct task *)frame->r0;
	unsigned long priority = frame->r1;

	scheduler_spin_lock();

	/* Make sure the task is alive */
	frame->r0 = scheduler_task_alive(task);
	if (frame->r0 != 0) {
		scheduler_spin_unlock();
		return;
	}

	assert(task->marker == SCHEDULER_TASK_MARKER);

	task->base_priority = priority;
	/* if (task->base_priority < task->current_priority) Will the cause a priority inheritance problem????? */
	sched_queue_reprioritize(task, priority);

	/* Let the context switcher sort this out */
	scheduler_request_switch(scheduler_current_core());
	scheduler_spin_unlock();

}

static bool scheduler_is_viable(void)
{
	bool viable = false;

	/* Well look for blocked tasks schedule task list */
	struct task *potential_task;
	sched_list_for_each_entry(potential_task, &scheduler->tasks, scheduler_node) {
		assert(potential_task->marker == SCHEDULER_TASK_MARKER);
		if (potential_task->state >= TASK_BLOCKED && (potential_task->flags & SCHEDULER_IGNORE_VIABLE) == 0) {
			viable = true;
			break;
		}
	}

	/* Not viable, we should be cleanup and shutdown */
	return viable;
}

__weak void scheduler_terminated_hook(struct task *task)
{
	assert(task != 0 && task->marker == SCHEDULER_TASK_MARKER);
	if (task->exit_handler)
		task->exit_handler(task);
}

struct task *debug_tasks[25];

__weak void scheduler_idle_hook(void)
{
	scheduler_spin_unlock();
	__WFI();
	scheduler_spin_lock();

	assert(cls_datum(current_task == 0));
}

__weak unsigned long scheduler_num_cores(void)
{
	return 1;
}

__weak unsigned long scheduler_current_core(void)
{
	return 0;
}

__weak void scheduler_request_switch(unsigned long core)
{
	/* Drop if kicking other cores */
	if (core == UINT32_MAX)
		return;

	/* Pend the pendsv interrupt */
	SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;

	/* Make sure this is visible, not required but architecturally recommended */
	__DSB();
}

struct scheduler_frame *scheduler_switch(struct scheduler_frame *frame)
{
	struct task *expired;

	assert(scheduler != 0);

	scheduler_spin_lock();

	/* Get the current task */
	struct task *task = sched_set_current(0);
	struct task *last_task = task;

	/* Only push the current task if we have one and the scheduler is not locked */
	if (task != 0) {

		assert(task->marker == SCHEDULER_TASK_MARKER && task->state == TASK_RUNNING);

		/* No switch if scheduler is locked */
		if (scheduler->locked < 0) {
			sched_set_current(task);
			scheduler_spin_unlock();
			return frame;
		}

		/* Force the running task to complete for the processor */
		task->state = TASK_READY;
		task->core = UINT32_MAX;
		task->psp = frame;
		sched_queue_push(&scheduler->ready_queue, task);
	}

	/* Try to get the next task */
	while (true) {

		/* Check for deferred wake ups */
		for (int i = 0; (cls_datum(taken_wake_counter) ^ cls_datum(given_wake_counter)) != 0 && i < SCHEDULER_MAX_DEFERED_WAKE; ++i) {
			unsigned long wakeup = atomic_exchange(&cls_datum(deferred_wake)[i], 0);
			if (wakeup != 0) {
				scheduler_wake_futex((struct futex *)(wakeup & ~0x00000001), wakeup & 0x00000001);
				++cls_datum(taken_wake_counter);
			}
		}

		/* Ready any expired timers */
		while((expired = scheduler_timer_pop()) != 0) {

			assert(expired->marker == SCHEDULER_TASK_MARKER);

			/* Remove from any wait queue */
			sched_queue_remove(expired);

			/* Make ready */
			expired->state = TASK_READY;
			expired->psp->r0 = (uint32_t)-ETIMEDOUT;

			/* Add to the ready queue */
			sched_queue_push(&scheduler->ready_queue, expired);
		}

		/* Try to get highest priority ready task */
		task = sched_queue_pop(&scheduler->ready_queue, scheduler_current_core());
		if (task) {

			assert(task->marker == SCHEDULER_TASK_MARKER);

			/* Is the stack good? */
			if (scheduler_check_stack(task))
				break;

			/* Sadness but evict the task */
			task->state = TASK_TERMINATED;
			task->psp->r0 = (uint32_t)-EFAULT;
			sched_queue_remove(task);
			sched_list_remove(&task->timer_node);
			sched_list_remove(&task->scheduler_node);
			scheduler_terminated_hook(task);
		}

		/* If no potential tasks, try to terminate the scheduler */
		if (!scheduler_is_viable() && cls_datum(scheduler_initial_frame) != 0) {

			/* The syscall will return ok */
			cls_datum(scheduler_initial_frame)->r0 = 0;

			/* Let the wolves out to play */
			scheduler_spin_unlock();

			/* This will return to the invoker of scheduler_start */
			return cls_datum(scheduler_initial_frame);
		}

		/* Call the idle hook if present */
		scheduler_idle_hook();
	}

	/* Mark the task as running and return its scheduler frame */
	assert(task->state == TASK_READY);
	task->state = TASK_RUNNING;
	task->core = scheduler_current_core();

	/* Update the slice expires if needed */
	if (task != last_task || cls_datum(slice_expires) < 0)
		cls_datum(slice_expires) = scheduler->slice_duration;

	/* Update the current task */
	if (sched_set_current(task) != 0)
		abort();

	/* If there are still more ready tasks, kick other cores if need to ensure high priority tasks run */
	if (!sched_queue_empty(&scheduler->ready_queue)) {

		/* Check other cores */
		for (unsigned long core = 0; core < scheduler_num_cores(); ++core) {

			/* Other cores */
			if (core != scheduler_current_core()) {

				/* Only kick the other core if there is a higher priority task to run */
				struct task *core_task = cls_datum_core(core, current_task);
				if (core_task) {

					/* Well check the priority taking into account core affinity */
					struct task *cursor;
					sched_list_for_each_entry(cursor, &scheduler->ready_queue.tasks, queue_node) {
						if ((cursor->flags & SCHEDULER_CORE_AFFINITY) == 0 || cursor->affinity == core) {
							if (cursor->current_priority < core_task->current_priority)
								scheduler_request_switch(core);
							break;
						}
					}
				}
			}
		}
	}

	/* Let the wolves out to play */
	scheduler_spin_unlock();

	/* Use this frame */
	return task->psp;
}

struct task *scheduler_create(void *stack, size_t stack_size, const struct task_descriptor *descriptor)
{
	/* We must have a valid descriptor and stack */
	if (!stack || !scheduler || !descriptor) {
		errno = EINVAL;
		return 0;
	}

	/* Initialize the stack for simple stack consumption measurements */
	if (descriptor->flags & SCHEDULER_TASK_STACK_CHECK) {
		unsigned long *pos = stack;
		unsigned long *end = (void *)(stack + stack_size);
		while (pos < end)
			*pos++ = SCHEDULER_STACK_MARKER;
	}

	/* Initialize the task and add to the scheduler task list */
	struct task *task = stack;
	task->marker = SCHEDULER_TASK_MARKER;
	sched_list_init(&task->timer_node);
	sched_list_init(&task->scheduler_node);
	sched_list_init(&task->queue_node);
	sched_list_init(&task->owned_futexes);
	task->current_queue = 0;
	task->timer_expires = UINT32_MAX;
	task->base_priority = descriptor->priority;
	task->current_priority = descriptor->priority;
	task->exit_handler = descriptor->exit_handler;
	task->flags = descriptor->flags;
	task->context = descriptor->context;
	task->core = UINT32_MAX;
	task->affinity = descriptor->flags & SCHEDULER_CORE_AFFINITY ? descriptor->affinity : UINT32_MAX;

	/* Build the scheduler frame to use the PSP and run in privileged mode */
	if ((descriptor->flags & SCHEDULER_NO_FRAME_INIT) == 0) {
		task->psp = (struct scheduler_frame *)((((uintptr_t)stack) + stack_size - sizeof(struct scheduler_frame)) & ~7);
		task->psp->exec_return = 0xfffffffd;
		task->psp->control = CONTROL_SPSEL_Msk;
		task->psp->pc = ((uint32_t)descriptor->entry_point & ~0x01UL);
		task->psp->lr = 0;
		task->psp->psr = xPSR_T_Msk;
		task->psp->r0 = (uint32_t)task->context;
	}

#ifdef BUILD_TYPE_DEBUG
	task->psp->r1 = 0xdead0001;
	task->psp->r2 = 0xdead0002;
	task->psp->r3 = 0xdead0003;
	task->psp->r4 = 0xdead0004;
	task->psp->r5 = 0xdead0005;
	task->psp->r6 = 0xdead0006;
	task->psp->r7 = 0xdead0007;
	task->psp->r8 = 0xdead0008;
	task->psp->r9 = 0xdead0009;
	task->psp->r10 = 0xdead000a;
	task->psp->r11 = 0xdead000b;
	task->psp->r12 = 0xdead000c;
#endif

	/* Initialize the TLS pointer is not suppressed */
	if ((descriptor->flags & SCHEDULER_NO_TLS_INIT) == 0) {

		/* Set the TLS pointer */
		task->tls = (void *)task + ALIGNMENT_ROUND_TYPE(struct task, 8);

		/* Initialize the TLS block */
		scheduler_tls_init_hook(task->tls);

		/* And now the stack marker */
		task->stack_marker = task->tls + scheduler->tls_size;
	}

	/* Double check the stack marker */
	assert(scheduler_check_stack(task));

	/* Handle primordial task specially, very hacky to support threads and pthreads initialization */
	if (descriptor->flags & SCHEDULER_PRIMORDIAL_TASK) {

		/* Add the task the scheduler list */
		sched_list_push(&scheduler->tasks, &task->scheduler_node);

		/* Force core affinity */
		task->flags |= SCHEDULER_CORE_AFFINITY;
		task->affinity = scheduler_current_core();

		/* Mark as running */
		task->state = TASK_RUNNING;
		task->core = task->affinity;
		cls_datum(current_task) = task;

		/* If the tls pointer was initialized, the forward to the switch hook */
		if (task->tls != 0)
			scheduler_switch_hook(task);

		/* All done with the primorial task */
		return task;
	}

	/* Ask scheduler to add the new task */
	return (struct task *)svc_call1(SCHEDULER_CREATE_SVC, (uint32_t)task);
}

int scheduler_init(struct scheduler *new_scheduler, size_t tls_size)
{
	/* Make sure some memory was provided */
	if (!new_scheduler) {
		errno = EINVAL;
		return -EINVAL;
	}

	/* This pulls in the layout support structures */
	enable_debugger_support();

	/* Mark the services which need a scheduler frame create in entrance */
	/* TODO NEED A BETTER WAY I.E. Compile time */
	scheduler_svc_vector[SCHEDULER_SUSPEND_SVC] |= SCHEDULER_FRAME_NEEDED;
	scheduler_svc_vector[SCHEDULER_WAIT_SVC] |= SCHEDULER_FRAME_NEEDED;

	/* Initialize the scheduler */
	memset(new_scheduler, 0, sizeof(struct scheduler));
	new_scheduler->marker = SCHEDULER_MARKER;
	new_scheduler->slice_duration = SCHEDULER_TIME_SLICE;
	new_scheduler->tls_size = tls_size;
	new_scheduler->locked = 0;
	new_scheduler->timer_expires = UINT32_MAX;
	new_scheduler->critical = UINT32_MAX;
	new_scheduler->critical_counter = 0;
	sched_queue_init(&new_scheduler->ready_queue);
	sched_list_init(&new_scheduler->timers);
	sched_list_init(&new_scheduler->tasks);

	/* Initialize the all core local data */
	for (unsigned long core = 0; core < scheduler_num_cores(); ++core) {
		cls_datum_core(core, scheduler_initial_frame) = 0;
		cls_datum_core(core, current_task) = 0;
		cls_datum_core(core, slice_expires) = INT32_MAX;
		cls_datum_core(core, ticks) = 0;
		memset(cls_datum_core_ptr(core, deferred_wake), 0, sizeof(deferred_wake));
	}

	/* Save a scheduler singleton */
	scheduler = new_scheduler;

	/* All good */
	return 0;
}

int scheduler_run(void)
{
	/* Make sure the scheduler has been initialized */
	if (!scheduler) {
		errno = EINVAL;
		return -EINVAL;
	}

	/* Forward start hook */
	scheduler_startup_hook();

	/* We only return from this when the scheduler is shutdown */
	++scheduler->running;
	int result = svc_call0(SCHEDULER_START_SVC);
	if (result < 0) {
		errno = -result;
		return result;
	}
	--scheduler->running;

	/* Forward exit hook */
	scheduler_shutdown_hook();

	/* All done */
	return 0;
}

bool scheduler_is_running(void)
{
	/* We are running if the initial frame has be set by the start service call */
	return scheduler != 0 && scheduler->running > 0;
}

unsigned long scheduler_enter_critical(void)
{
	assert(scheduler_is_running());

	/* When we leave this function, 1) Interrupts are disabled, 2) we are hold the scheduler spin lock */
	uint32_t state = disable_interrupts();

	/* Do already owne the critical section? */
	if (scheduler->critical == scheduler_current_core()) {
		++scheduler->critical_counter;
		return state;
	}

	/* Loop trying to get the critical section */
	while (true) {

		/* In a multicore we need the spinlock */
		scheduler_spin_lock();

		/* Try to grap the critical sections supporting recursive entrances, keep the spinlock if successful */
		if (scheduler->critical == UINT32_MAX || scheduler->critical == scheduler_current_core()) {
			scheduler->critical = scheduler_current_core();
			++scheduler->critical_counter;
			return state;
		}

		/* Release the spin lock and wait until someone unlock or at least calls SEV */
		scheduler_spin_unlock();
	}
}

void scheduler_exit_critical(unsigned long state)
{
	assert(scheduler_is_running() && scheduler->critical == scheduler_current_core());

	/* Handle nested critical sections */
	if (--scheduler->critical_counter >= 1)
		return;

	/* Release the critical section */
	scheduler->critical = UINT32_MAX;

	/* Now the spin lock and the interrupts */
	scheduler_spin_unlock();
	enable_interrupts(state);
}

int scheduler_lock(void)
{
	assert(scheduler_is_running());

	/* Yes this implements atomic_fetch_sub */
	return scheduler->locked--;
}

int scheduler_unlock(void)
{
	assert(scheduler_is_running());

	/* Yes this implements atomic_fetch_add */
	return scheduler->locked++;
}

int scheduler_lock_restore(int state)
{
	assert(scheduler_is_running());

	return atomic_exchange(&scheduler->locked, state);
}

bool scheduler_is_locked(void)
{
	assert(scheduler_is_running());
	return scheduler->locked < 0;
}

void scheduler_yield(void)
{
	/* Ignore is scheduler is locked */
	if (scheduler->locked < 0)
		return;

	/* We need a service call so that we appear to resume on return from this call */
	(void)svc_call0(SCHEDULER_YIELD_SVC);
}

struct task *scheduler_task(void)
{
	/* We are the current running task */
	return scheduler != 0 ? sched_get_current() : 0;
}

int scheduler_sleep(unsigned long ticks)
{
	/* Msecs should be greater 0, just yield otherwise */
	if (ticks == 0) {
		scheduler_yield();
		return 0;
	}

	/* We are timed suspending ourselves */
	int status = svc_call2(SCHEDULER_SUSPEND_SVC, (uint32_t)scheduler_task(), ticks);
	if (status < 0 && status != -ETIMEDOUT) {
		errno = -status;
		return status;
	}

	/* All good */
	return 0;
}

int scheduler_suspend(struct task *task)
{
	/* Are we suspending ourselves? */
	if (!task)
		task = scheduler_task();

	if (scheduler_num_cores() > 1 && task != scheduler_task()) {
		errno = EINVAL;
		return -errno;
	}

	/* Suspend it */
	int status = svc_call2(SCHEDULER_SUSPEND_SVC, (uint32_t)task, SCHEDULER_WAIT_FOREVER);
	if (status < 0) {
		errno = -status;
		return status;
	}

	/* All good */
	return 0;
}

int scheduler_resume(struct task *task)
{
	assert(task != 0);

	/* Make the task ready to run */
	int status = svc_call1(SCHEDULER_RESUME_SVC, (uint32_t)task);
	if (status < 0)
		errno = -status;

	/* All good */
	return status;
}

int scheduler_terminate(struct task *task)
{
	/* Use the current task if needed */
	if (!task)
		task = scheduler_task();

	if (scheduler_num_cores() > 1 && task != scheduler_task()) {
		errno = EINVAL;
		return -errno;
	}

	/* Forward */
	int status = svc_call1(SCHEDULER_TERMINATE_SVC, (uint32_t)task);
	if (status < 0) {
		errno = -status;
		return status;
	}

	return status;
}

void scheduler_futex_init(struct futex *futex, long *value, unsigned long flags)
{
	assert(futex != 0);

	futex->marker = SCHEDULER_FUTEX_MARKER;
	futex->value = value;
	futex->flags = flags;
	sched_queue_init(&futex->waiters);
	sched_list_init(&futex->owned);
}

int scheduler_futex_wait(struct futex *futex, long value, unsigned long ticks)
{
	assert(futex != 0 && futex->marker == SCHEDULER_FUTEX_MARKER);

	int status = svc_call3(SCHEDULER_WAIT_SVC, (uint32_t)futex, value, ticks);
	if (status < 0)
		errno = -status;

	return status;
}

int scheduler_futex_wake(struct futex *futex, bool all)
{
	assert(futex != 0 && futex->marker == SCHEDULER_FUTEX_MARKER);

	/* Do it the hard way? */
	if (is_interrupt_context()) {

		/* Make the contention tracking and priority inheritance is disabled */
		if ((futex->flags & (SCHEDULER_FUTEX_PI | SCHEDULER_FUTEX_OWNER_TRACKING)) != 0) {
			errno = EINVAL;
			return -EINVAL;
		}

		/* Find an empty slot */
		unsigned long expected = 0;
		unsigned long wakeup = (unsigned long)futex | all;
		for (int i = 0; i < SCHEDULER_MAX_DEFERED_WAKE; ++i) {
			/* The second clause protects from multiple wakeups against the same futex */
			if (atomic_compare_exchange_strong(&cls_datum(deferred_wake)[i], &expected, wakeup)) {
				++cls_datum(given_wake_counter);
				scheduler_request_switch(scheduler_current_core());
				return 0;
			} else if (expected == wakeup)
				return 0;

			/* Get ready to try the next slot */
			expected = 0;
		}

		/* Very bad, resource exhausted */
		errno = ENOSPC;
		return -ENOSPC;
	}

	/* Send to the wake service */
	int status = svc_call2(SCHEDULER_WAKE_SVC, (uint32_t)futex, all);
	if (status < 0)
		errno = -status;

	return status;
}

int scheduler_set_priority(struct task *task, unsigned long priority)
{
	/* Range check the new priority */
	if (priority > SCHEDULER_MIN_TASK_PRIORITY) {
		errno = -EINVAL;
		return -EINVAL;
	}

	/* Use the current task if needed */
	if (!task)
		task = scheduler_task();

	/* Forward to the service handler */
	return svc_call2(SCHEDULER_PRIORITY_SVC, (uint32_t)task, priority);
}

unsigned long scheduler_get_priority(struct task *task)
{
	/* Use the current task if needed */
	if (!task)
		task = scheduler_task();

	return task->current_priority;
}

void scheduler_set_flags(struct task *task, unsigned long mask)
{
	/* Use the current task if needed */
	if (!task)
		task = scheduler_task();

	assert(task != 0 && task->marker == SCHEDULER_TASK_MARKER);

	task->flags |= mask;
}

void scheduler_clear_flags(struct task *task, unsigned long mask)
{
	/* Use the current task if needed */
	if (!task)
		task = scheduler_task();

	assert(task != 0 && task->marker == SCHEDULER_TASK_MARKER);

	task->flags &= ~mask;
}

unsigned long scheduler_get_flags(struct task *task)
{
	/* Use the current task if needed */
	if (!task)
		task = scheduler_task();

	assert(task != 0 && task->marker == SCHEDULER_TASK_MARKER);

	return task->flags;
}

void scheduler_for_each(struct sched_list *list, for_each_sched_node_t func, void *context)
{
	scheduler_lock();
	struct sched_list *current;
	sched_list_for_each(current, list)
		if (!func(current, context))
			break;
	scheduler_unlock();
}

enum task_state scheduler_get_state(struct task *task)
{
	/* Use the current task if needed */
	if (!task)
		task = scheduler_task();

	assert(task != 0 && task->marker == SCHEDULER_TASK_MARKER);

	return task->state;
}
