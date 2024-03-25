#ifndef __THREADS_H_
#define __THREADS_H_

#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>

#include <sys/cdefs.h>

#include <linked-list.h>

#include <picotls.h>

#include <pico/toolkit/scheduler.h>

#ifndef container_of
#define container_of(ptr, type, member) __containerof(ptr, type, member)
#endif

#define __THRD_STACK_SIZE 1024
#define __THRD_PRIORITY (SCHEDULER_NUM_TASK_PRIORITIES / 2)
#define __THRD_KEYS_MAX 8
#define __THRD_TSS_SIZE (sizeof(void *) * __THRD_KEYS_MAX)
#define __THRD_MARKER 0x137cc731UL

#define TSS_DTOR_ITERATIONS 5

#define ONCE_FLAG_INIT 0

typedef atomic_int once_flag;
typedef uintptr_t thrd_t;
typedef size_t tss_t;

enum {
	mtx_prio_inherit = 0x8
};

typedef struct mtx
{
	long value;
	struct futex futex;
	unsigned long type;
	long count;
} mtx_t;

typedef struct cnd
{
	struct mtx *mutex;
	unsigned long sequence;
	struct futex futex;
} cnd_t;

struct tss
{
	atomic_bool used;
	void (*destructor)(void *);
};

struct thrd
{
	int (*func)(void *);
	void *context;
	int ret;
	bool detached;
	bool terminated;
	thrd_t joiner;
	struct cnd joiners;
	struct linked_list thrd_node;
	unsigned long marker;
	char stack[] __attribute__((aligned(8)));
};

struct thrd_attr
{
	unsigned long flags;
	unsigned long priority;
	size_t stack_size;
};

void _thdr_attr_init(struct thrd_attr *attr, unsigned long flags, unsigned long priority, size_t stack_size);
int	_thrd_create(thrd_t *thrd, int (*func)(void *), void *arg, struct thrd_attr *attr);
int _thrd_sleep(unsigned long msec);

#endif
