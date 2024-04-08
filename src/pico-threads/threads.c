#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#include <picotls.h>

#include <pico/toolkit/tls.h>
#include <pico/toolkit/compiler.h>

struct arguments
{
	int argc;
	char **argv;
	int ret;
};

/* Memory management hooks */
extern void *_thrd_alloc(size_t size);
extern void _thrd_release(void *ptr);

/* Needed to wrap the main thread */
extern void scheduler_startup_hook(void);
extern char __arm32_tls_tcb_offset;

static struct tss tss_map[__THRD_KEYS_MAX] = { [0 ... __THRD_KEYS_MAX - 1] = { .used = false, .destructor = 0 } };
static once_flag thrds_reaper_init_flag = ONCE_FLAG_INIT;
static once_flag thrds_init_flag = ONCE_FLAG_INIT;
static cnd_t thrds_reap;
static mtx_t thrds_lock;
static struct linked_list thrds;

static struct scheduler scheduler;

__weak void *_thrd_alloc(size_t size)
{
	return calloc(1, size);
}

__weak void _thrd_release(void *ptr)
{
	free(ptr);
}

void call_once(once_flag *flag, void (*func)(void))
{
	/* All ready done */
	if (atomic_load(flag) == 2)
		return;

	/* Setup a futex to wait on */
	struct futex futex;
	scheduler_futex_init(&futex, (long *)flag, 0);

	/* Try to claim the initializer */
	int expected = 0;
	if (!atomic_compare_exchange_strong(flag, &expected, 1)) {

		/* Wait on the futex */
		expected = 2;
		while (!atomic_compare_exchange_strong(flag, &expected, 2)) {
			scheduler_futex_wait(&futex, expected, SCHEDULER_WAIT_FOREVER);
			expected = 2;
		}

		/* Done */
		return;
	}

	/* Run the function */
	func();

	/*  Mark as done */
	atomic_store(flag, 2);

	/* Wake any waiters */
	scheduler_futex_wake(&futex, true);
}

void cnd_destroy(cnd_t *cnd)
{
}

int cnd_init(cnd_t *cnd)
{
	assert(cnd != 0);

	/* Initialize */
	cnd->mutex = 0;
	cnd->sequence = 0;
	scheduler_futex_init(&cnd->futex, (long *)&cnd->sequence, 0);

	/* Great */
	return thrd_success;
}

static int _cnd_wait(cnd_t *cnd, mtx_t *mtx, unsigned long msec)
{
	assert(cnd != 0 && mtx != 0);

	int sequence = cnd->sequence;
	struct mtx *expected = 0;

	if (cnd->mutex != mtx) {
		atomic_compare_exchange_strong(&cnd->mutex, &expected, mtx);
		if (cnd->mutex != mtx) {
			errno = EINVAL;
			return thrd_error;
		}
	}

	mtx_unlock(cnd->mutex);
	int status = scheduler_futex_wait(&cnd->futex, sequence, msec);
	mtx_lock(cnd->mutex);

	/* Did we timeout or have an error */
	if (status < 0) {
		errno = -status;
		return status == -ETIMEDOUT ? thrd_timedout : thrd_error;
	}

	return thrd_success;
}

int	cnd_wait(cnd_t *cnd, mtx_t *mtx)
{
	return _cnd_wait(cnd, mtx, SCHEDULER_WAIT_FOREVER);
}

int	cnd_timedwait(cnd_t *cnd, mtx_t *mtx, const struct timespec *tm)
{
	assert(tm != 0);

	unsigned long ticks = scheduler_get_ticks();
	unsigned long long tm_ticks = (tm->tv_sec * 1000) + (tm->tv_nsec / 1000000);

	/* Have we already miss the timeout? */
	if (ticks < tm_ticks || tm_ticks - ticks >= UINT32_MAX)
		return thrd_timedout;

	return _cnd_wait(cnd, mtx, tm_ticks - ticks);
}

static int _cnd_wakeup(struct cnd *cnd, bool all)
{
	assert(cnd != 0);

	/* We are waking someone up */
	atomic_fetch_add(&cnd->sequence, 1);

	/* Wake some waiters */
	scheduler_futex_wake(&cnd->futex, all);

	/* March on */
	return thrd_success;
}

int	cnd_signal(cnd_t *cnd)
{
	return _cnd_wakeup(cnd, false);
}

int	cnd_broadcast(cnd_t *cnd)
{
	return _cnd_wakeup(cnd, true);
}

int mtx_init(mtx_t *mtx, int type)
{
	assert(mtx != 0);

	/* Initialize it */
	mtx->value = 0;
	mtx->type = type;
	mtx->count = 0;
	scheduler_futex_init(&mtx->futex, &mtx->value, (type & mtx_prio_inherit) ? SCHEDULER_FUTEX_PI | SCHEDULER_FUTEX_OWNER_TRACKING | SCHEDULER_FUTEX_CONTENTION_TRACKING : SCHEDULER_FUTEX_OWNER_TRACKING | SCHEDULER_FUTEX_CONTENTION_TRACKING);

	/* All good */
	return thrd_success;
}

void mtx_destroy(mtx_t *mtx)
{
}

int mtx_trylock(mtx_t *mtx)
{
	assert(mtx != 0);

	long value = (long)scheduler_task();

	/* Handle recursive locks */
	if ((mtx->type & mtx_recursive) && mtx->value == value) {
		++mtx->count;
		return thrd_success;
	}

	/* Just try update the lock bit */
	long expected = 0;
	if (!atomic_compare_exchange_strong(&mtx->value, &expected, value)) {
		errno = EBUSY;
		return thrd_busy;
	}

	/* Initialize the count for recursive locks */
	if (mtx->type & mtx_recursive)
		mtx->count = 1;

	/* Got the lock */
	return thrd_success;
}

static int _mtx_lock(mtx_t *mtx, unsigned long msec)
{
	assert(mtx != 0);

	/* Handle recursive locks */
	long value = (long)scheduler_task();
	assert(value != 0);
	if (value == (mtx->value & ~SCHEDULER_FUTEX_CONTENTION_TRACKING)) {
		if ((mtx->type & mtx_recursive) == 0) {
			errno = EINVAL;
			return thrd_error;
		}
		++mtx->count;
		return thrd_success;
	}

	/* Run the lock algo */
	long expected = 0;
	while (!atomic_compare_exchange_strong(&mtx->value, &expected, value)) {

		/* We did not get the lock, wait for it */
		int status = scheduler_futex_wait(&mtx->futex, expected, msec);
		if (status < 0) {
			errno = -status;
			return status == -ETIMEDOUT ? thrd_timedout : thrd_error;
		}

		/* We have requested contention tracking, we might own the mutex now */
		if (value == (mtx->value & ~SCHEDULER_FUTEX_CONTENTION_TRACKING))
			break;

		/* No we did not end up ownership, try again */
		expected = 0;
	}

	/* Initialize the count for recursive locks */
	if (mtx->type & mtx_recursive)
		mtx->count = 1;

	/* Locked */
	return thrd_success;
}

int mtx_lock(mtx_t *mtx)
{
	return _mtx_lock(mtx, SCHEDULER_WAIT_FOREVER);
}

int mtx_timedlock(mtx_t *mtx, const struct timespec *tm)
{
	assert(mtx != 0 && tm != 0);

	/* Is this a timed locked? */
	if ((mtx->type & mtx_timed) == 0) {
		errno = EINVAL;
		return thrd_error;
	}

	unsigned long ticks = scheduler_get_ticks();
	unsigned long long tm_ticks = (tm->tv_sec * 1000) + (tm->tv_nsec / 1000000);

	/* Have we already miss the timeout? */
	if (ticks < tm_ticks || tm_ticks - ticks >= UINT32_MAX)
		return thrd_timedout;

	return _mtx_lock(mtx, tm_ticks - ticks);
}

int mtx_unlock(mtx_t *mtx)
{
	assert(mtx != 0);

	/* Make sure we are the locker */
	long value = (long)scheduler_task();
	if (value != (mtx->value & ~SCHEDULER_FUTEX_CONTENTION_TRACKING)) {
		errno = EINVAL;
		return thrd_error;
	}

	/* Handle recursive lock */
	if ((mtx->type & mtx_recursive) && --mtx->count > 0)
		return thrd_success;

	/* Hot path unlock in the non-contented case */
	long expected = value;
	if (mtx->value == expected && atomic_compare_exchange_strong(&mtx->value, &expected, 0))
		return thrd_success;

	/* Must have been contended */
	int status = scheduler_futex_wake(&mtx->futex, false);
	if (status < 0) {
		errno = -status;
		return thrd_error;
	}

	/* All good */
	return thrd_success;
}

int tss_create(tss_t *tss_key, tss_dtor_t destructor)
{
	/* Look for a free tss slot */
	for (int i = 0; i < __THRD_KEYS_MAX; ++i)
		if (!atomic_exchange(&tss_map[i].used, true)) {
			tss_map[i].destructor = destructor;
			*tss_key = i;
			return thrd_success;
		}

	/* Could not find a free slot */
	errno = EAGAIN;
	return thrd_error;
}

void tss_delete(tss_t tss_key)
{
	if (tss_key > __THRD_KEYS_MAX)
		return;
	tss_map[tss_key].used = false;
	tss_map[tss_key].destructor = 0;
}

void *tss_get(tss_t tss_key)
{
	if (tss_key > __THRD_KEYS_MAX || !tss_map[tss_key].used)
		return 0;

	/* Return the value of the key */
	return ((struct thrd *)(scheduler_task()->context))->tss[tss_key];
}

int	tss_set(tss_t tss_key, void *val)
{
	if (tss_key > __THRD_KEYS_MAX || !tss_map[tss_key].used)
		return thrd_error;

	/* Save the value */
	((struct thrd *)(scheduler_task()->context))->tss[tss_key] = val;
	return thrd_success;
}

static void thdr_dispatch(void *context)
{
	struct thrd *thrd = context;

	assert(context != 0);

	/* Forward */
	thrd_exit(thrd->func(thrd->context));
}

static void thrd_exit_handler(struct task *task)
{
	assert(task != 0);

	struct thrd *thread = task->context;

	/* Mark the thread as terminated */
	thread->terminated = true;

	/* Wake the joiner of the thread reaper */
	cnd_t *cnd = thread->detached ? &thrds_reap : &thread->joiners;

	/* Kick the joiner crew */
	if (cnd_broadcast(cnd) != thrd_success)
		abort();
}

static void thrds_init(void)
{
	/* Initialize the scheduler */
	scheduler_init(&scheduler, _tls_size());

	/* Setup the the threads list for cleanup support */
	list_init(&thrds);
	if (mtx_init(&thrds_lock, mtx_plain) != thrd_success)
		abort();

	/* Need a thread structure to wrap the main thread */
	struct thrd *thread = _thrd_alloc(sizeof(struct thrd) + sizeof(struct task));
	if (!thread)
		abort();

	/* Setup the main task */
	struct task_descriptor main_desc = { .entry_point = 0, .exit_handler = thrd_exit_handler, .context = thread, .flags = SCHEDULER_NO_TLS_INIT | SCHEDULER_NO_FRAME_INIT | SCHEDULER_PRIMORDIAL_TASK, .priority = __THRD_PRIORITY };
	struct task *main_task = scheduler_create(thread->stack, 0, &main_desc);
	if (!main_task)
		abort();

	/* Fixup the task TLS pointer it was initialize by the tls infrastructure */
	main_task->tls = __aeabi_read_tp() + (size_t)&__arm32_tls_tcb_offset;

	/* Basic thread initialization */
	thread->func = 0;
	thread->context = 0;
	thread->ret = 0;
	thread->detached = false;
	thread->terminated = false;
	thread->joiner = 0;
	list_init(&thread->thrd_node);
	thread->marker = __THRD_MARKER;

	/* Initialize the joiner conditional */
	if (cnd_init(&thread->joiners) != thrd_success)
		abort();

	/* Add the wrapped thread to the thread list, not locked as no other iso thread should be alive */
	list_add(&thrds, &thread->thrd_node);

	/* Now startup the scheduler by call the start hook and mark as running */
	scheduler_startup_hook();
	scheduler.running = true;

	/* Now yield which send us through the scheduler and return here */
	scheduler_yield();
}

int	_thrd_create(thrd_t *thrd, int (*func)(void *), void *arg, thrd_attr_t *attr)
{
	call_once(&thrds_init_flag, thrds_init);

	/* Allocate the stack */
	struct thrd *thread = _thrd_alloc(attr->stack_size);
	if (!thread) {
		errno = ENOMEM;
		return thrd_error;
	}

	/* Initialize the thread block */
	thread->func = func;
	thread->context = arg;
	thread->detached = false;
	thread->terminated = false;
	thread->joiner = 0;
	thread->ret = 0;
	list_init(&thread->thrd_node);
	thread->marker = __THRD_MARKER;

	/* Initialize the joiner conditional */
	if (cnd_init(&thread->joiners) != thrd_success)
		return thrd_error;

	/* Initialize the task descriptor */
	struct task_descriptor desc;
	desc.entry_point = thdr_dispatch;
	desc.exit_handler = thrd_exit_handler;
	desc.context = thread;
	desc.flags = attr->flags;
	desc.priority = attr->priority;
	desc.affinity = attr->affinity;

	/* Carefully add to the threads list for clean up */
	if (mtx_lock(&thrds_lock) != thrd_success)
		goto error_release_thrd;

	list_add(&thrds, &thread->thrd_node);

	if (mtx_unlock(&thrds_lock) != thrd_success)
		goto error_remove_thrds;

	/* Launch the thread */
	if (!scheduler_create(thread->stack, attr->stack_size - sizeof(struct thrd), &desc))
		goto error_remove_thrds;

	/* Save the thread id, we are great! */
	*thrd = (thrd_t)thread;
	return thrd_success;

error_remove_thrds:
	if (mtx_lock(&thrds_lock) != thrd_success)
		abort();

	list_remove(&thrds);

	if (mtx_unlock(&thrds_lock) != thrd_success)
		abort();

error_release_thrd:
	_thrd_release(thread);

	return thrd_error;
}

int	thrd_create(thrd_t *thrd, thrd_start_t func, void *arg)
{
	assert(thrd != 0 && func != 0);
	struct thrd_attr attr = { .stack_size = __THRD_STACK_SIZE, .flags = 0, .priority = __THRD_PRIORITY, .affinity = UINT32_MAX, };
	return _thrd_create(thrd, func, arg, &attr);
}

thrd_t thrd_current(void)
{
	return (uintptr_t)scheduler_task()->context;
}

static int thrds_reaper(void *context)
{
	struct thrd *entry;
	struct thrd *current;

	/* Lock the thread list */
	if (mtx_lock(&thrds_lock) != thrd_success)
		abort();

	/* Loop forever until done */
	while (true) {

		/* Wait for some work */
		if (cnd_wait(&thrds_reap, &thrds_lock) != thrd_success)
			abort();

		/* Find all detached and terminated threads */
		list_for_each_entry_mutable(entry, current, &thrds, thrd_node) {
			if (entry->detached && entry->terminated) {
				list_remove(&entry->thrd_node);
				_thrd_release(entry);
			}
		}
	}

	/* Unlock the thread list */
	if (mtx_lock(&thrds_lock) != thrd_success)
		abort();

	/* All done */
	return 0;
}

static void thrds_reaper_init(void)
{
	/* Initialize the thread reaper synchronization */
	if (cnd_init(&thrds_reap) != thrd_success)
		abort();

	/* Start up the the reaper */
	thrd_t reaper;
	if (thrd_create(&reaper, thrds_reaper, 0) != thrd_success)
		abort();

	/* Mark reaper as detached */
	if (thrd_detach(reaper) != thrd_success)
		abort();
}

int	thrd_detach(thrd_t thrd)
{
	struct thrd *thread = (struct thrd *)thrd;

	/* Need a reaper thread to handle detached clean up */
	call_once(&thrds_reaper_init_flag, thrds_reaper_init);

	/* Get the tid, if zero use the current one */
	if (thrd == 0)
		thread = scheduler_task()->context;

	/* Mark the task */
	scheduler_set_flags((struct task *)thread->stack, SCHEDULER_IGNORE_VIABLE);

	/* Always good */
	return thrd_success;
}

int	thrd_equal(thrd_t lhs, thrd_t rhs)
{
	return lhs == rhs;
}

__noreturn void thrd_exit(int res)
{
	/* Clean up the the tss */
	void **tss = ((struct thrd *)(scheduler_task()->context))->tss;
	int i = 0;
	while (i < TSS_DTOR_ITERATIONS) {

		int more = __THRD_KEYS_MAX;
		for (int j = 0; j < __THRD_KEYS_MAX; ++j)
			if (tss_map[j].used && tss_map[j].destructor && tss[j] != 0) {
				void *target = tss[j];
				tss[j] = 0;
				tss_map[j].destructor(target);
			} else
				--more;

		/* Are we don */
		if (!more)
			break;

		/* Try again */
		++i;
	}

	/* Save the result code */
	((struct thrd *)scheduler_task()->context)->ret = res;

	/* Shoot ourselves */
	scheduler_terminate(0);

	/* We should never get here */
	abort();
}

int	thrd_join(thrd_t thrd, int *res)
{
	int status = thrd_success;

	assert(thrd != 0);

	/* We can not join detached threads */
	struct thrd *thread = (struct thrd *)thrd;

	/* Lock the thread list */
	if (mtx_lock(&thrds_lock) != thrd_success)
		abort();

	/* Validate thread is joinable */
	if (thread->detached || thread->joiner != 0) {
		if (mtx_unlock(&thrds_lock) != thrd_success)
			abort();
		return thrd_error;
	}

	/* Mark as joined */
	thread->joiner = thrd_current();

	/* While not terminated */
	while (!thread->terminated) {
		status = cnd_wait(&thread->joiners, &thrds_lock);
		if (status != thrd_success)
			break;
	}

	/* Remove thread the the list */
	list_remove(&thread->thrd_node);

	/* Release the thrd list lock */
	if (mtx_unlock(&thrds_lock) != thrd_success)
		abort();

	/* Save the return code */
	if (res)
		*res = thread->ret;

	/* Clean up memory */
	_thrd_release(thread);

	/* All good */
	return status;
}

int	thrd_sleep(const struct timespec *duration, struct timespec *remaining)
{
	assert(duration != 0);

	/* Convert to msecs */
	unsigned long msecs = (duration->tv_sec * 1000) + (duration->tv_nsec / 1000000);

	/* Sleep */
	int status = scheduler_sleep(msecs);

	/* No signals, if remaining provided initialize to zero */
	if (remaining) {
		remaining->tv_sec = 0;
		remaining->tv_nsec = 0;
	}

	return status;
}

void thrd_yield(void)
{
	scheduler_yield();
}

void _thdr_attr_init(thrd_attr_t *attr, unsigned long flags, unsigned long priority, size_t stack_size, unsigned long affinity)
{
	assert(attr != 0);

	attr->flags = flags;
	attr->priority = priority;
	attr->stack_size = stack_size;
	attr->affinity = affinity;
}
