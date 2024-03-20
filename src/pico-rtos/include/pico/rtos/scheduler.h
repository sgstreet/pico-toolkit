/*
 * Copyright (C) 2022 Stephen Street
 *
 * preemt-sched.h
 *
 * Created on: Feb 13, 2017
 *     Author: Stephen Street (stephen@redrocketcomputing.com)
 */


#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>

#include <sys/types.h>

#define TASK_NAME_LEN 32

#ifndef SCHEDULER_PRIOR_BITS
#define SCHEDULER_PRIOR_BITS 0x00000002UL
#endif

#ifndef SCHEDULER_REALTIME_IRQ_PRIORITY
#define SCHEDULER_REALTIME_IRQ_PRIORITY (0UL)
#endif

#ifndef SCHEDULER_MAX_IRQ_PRIORITY
#define SCHEDULER_MAX_IRQ_PRIORITY 1
#endif

#ifndef SCHEDULER_MIN_IRQ_PRIORITY
#define SCHEDULER_MIN_IRQ_PRIORITY (SCHEDULER_PRIOR_BITS + 1)
#endif

#define SCHEDULER_PENDSV_PRIORITY (SCHEDULER_MIN_IRQ_PRIORITY)
#define SCHEDULER_SVC_PRIORITY (SCHEDULER_MIN_IRQ_PRIORITY - 1)
#define SCHEDULER_SYSTICK_PRIORITY (SCHEDULER_REALTIME_IRQ_PRIORITY)

#define SCHEDULER_NUM_TASK_PRIORITIES 64UL
#define SCHEDULER_MAX_TASK_PRIORITY 0UL
#define SCHEDULER_MIN_TASK_PRIORITY (SCHEDULER_NUM_TASK_PRIORITIES - 1)

#define SCHEDULER_MARKER 0x13700731UL
#define SCHEDULER_TASK_MARKER 0x137aa731UL
#define SCHEDULER_FUTEX_MARKER 0x137bb731UL
#define SCHEDULER_STACK_MARKER 0x137cc731UL

#define SCHEDULER_WAIT_FOREVER 0xffffffffUL

#define SCHEDULER_IGNORE_VIABLE 0x00000001UL
#define SCHEDULER_TASK_STACK_CHECK 0x00000002UL

#define SCHEDULER_FUTEX_CONTENTION_TRACKING 0x00000001UL
#define SCHEDULER_FUTEX_PI 0x00000002UL
#define SCHEDULER_FUTEX_OWNER_TRACKING 0x00000004UL

#ifndef SCHEDULER_MAX_DEFERED_WAKE
#define SCHEDULER_MAX_DEFERED_WAKE 8
#endif

#ifndef SCHEDULER_TIME_SLICE
#define SCHEDULER_TIME_SLICE INT32_MAX
#endif

#ifndef SCHEDULER_MAIN_STACK_SIZE
#define SCHEDULER_MAIN_STACK_SIZE 4096UL
#endif

#ifndef SCHEDULER_TICK_FREQ
#define SCHEDULER_TICK_FREQ 1000UL
#endif

struct exception_frame
{
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
	uint32_t r12;
	uint32_t lr;
	uint32_t pc;
	uint32_t psr;
};

struct scheduler_frame
{
	uint32_t exec_return;
	uint32_t control;

	uint32_t r4;
	uint32_t r5;
	uint32_t r6;
	uint32_t r7;

	uint32_t r8;
	uint32_t r9;
	uint32_t r10;
	uint32_t r11;

	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
	uint32_t r12;
	uint32_t lr;
	uint32_t pc;
	uint32_t psr;
};

struct sched_list
{
	struct sched_list *next;
	struct sched_list *prev;
};

struct sched_queue
{
	unsigned int size;
	struct sched_list tasks;
};

enum task_state
{
	TASK_TERMINATED = 1,
	TASK_BLOCKED = 2,
	TASK_SUSPENDED = 4,
	TASK_READY = 5,
	TASK_RUNNING = 6,
	TASK_RESERVED = 0x7fffffff,
};

struct task;
typedef void (*task_entry_point_t)(void *context);
typedef void (*task_exit_handler_t)(struct task *task);
typedef bool (*for_each_sched_node_t)(struct sched_list *node, void *context);

struct task_descriptor
{
	task_entry_point_t entry_point;
	task_exit_handler_t exit_handler;
	void *context;
	unsigned long flags;
	unsigned long priority;
};

struct task
{
	/* This must be the first field, PendSV depends on it */
	struct scheduler_frame *psp;
	void *tls;
	unsigned long *stack_marker;

	enum task_state state;
	int core;

	unsigned long base_priority;
	unsigned long current_priority;

	unsigned long timer_expires;
	struct sched_list timer_node;

	struct sched_list scheduler_node;
	struct sched_list owned_futexes;

	struct sched_queue *current_queue;
	struct sched_list queue_node;

	void *context;
	task_exit_handler_t exit_handler;
	atomic_ulong flags;

	unsigned long marker;
};

struct futex
{
	long *value;
	struct sched_queue waiters;
	struct sched_list owned;
	unsigned long flags;
	unsigned long marker;
};

struct scheduler
{
	size_t tls_size;
	unsigned long slice_duration;

	struct sched_queue ready_queue;
	struct sched_queue suspended_queue;

	struct sched_list tasks;
	struct sched_list timers;
	unsigned long timer_expires;

	atomic_int locked;
	atomic_int critical;
	int critical_counter;

	atomic_int active_cores;

	unsigned long marker;
};

int scheduler_init(struct scheduler *new_scheduler, size_t tls_size);
int scheduler_run(void);
bool scheduler_is_running(void);
void scheduler_for_each(struct sched_list *list, for_each_sched_node_t func, void *context);

unsigned long scheduler_num_cores(void);
unsigned long scheduler_current_core(void);
void scheduler_request_switch(unsigned long core);

void scheduler_tick(void);

unsigned long scheduler_get_ticks(void);

struct task *scheduler_create(void *stack, size_t stack_size, const struct task_descriptor *descriptor);
struct task *scheduler_task(void);

unsigned long scheduler_enter_critical(void);
void scheduler_exit_critical(unsigned long state);

int scheduler_lock(void);
int scheduler_unlock(void);
int scheduler_lock_restore(int lock);
bool scheduler_is_locked(void);

void scheduler_yield(void);
int scheduler_sleep(unsigned long ticks);

int scheduler_suspend(struct task *task);
int scheduler_resume(struct task *task);
int scheduler_terminate(struct task *task);

void scheduler_futex_init(struct futex *futex, long *value, unsigned long flags);
int scheduler_futex_wait(struct futex *futex, long value, unsigned long ticks);
int scheduler_futex_wake(struct futex *futex, bool all);

int scheduler_set_priority(struct task *task, unsigned long priority);
unsigned long scheduler_get_priority(struct task *task);

void scheduler_set_flags(struct task *task, unsigned long mask);
void scheduler_clear_flags(struct task *task, unsigned long mask);
unsigned long scheduler_get_flags(struct task *task);

enum task_state scheduler_get_state(struct task *task);

#endif
