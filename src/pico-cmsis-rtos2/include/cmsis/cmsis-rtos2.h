#ifndef _RTOS_TOOLKIT_CMSIS_OS2_H_
#define _RTOS_TOOLKIT_CMSIS_OS2_H_

#include <stdatomic.h>
#include <stdint.h>

#include <pico/toolkit/cmsis.h>
#include <pico/toolkit/compiler.h>
#include <pico/toolkit/linked-list.h>
#include <pico/toolkit/spinlock.h>
#include <pico/rtos.h>

#include <cmsis/cmsis_os2.h>

#define RTOS_KERNEL_MARKER 0x42000024UL
#define RTOS_THREAD_MARKER 0x42011024UL
#define RTOS_MUTEX_MARKER 0x42022024UL
#define RTOS_MEMORY_POOL_MARKER 0x42033024UL
#define RTOS_SEMAPHORE_MARKER 0x42044024UL
#define RTOS_EVENTFLAGS_MARKER 0x42055024UL
#define RTOS_TIMER_MARKER 0x42066024UL
#define RTOS_MESSAGE_QUEUE_MARKER 0x42077024UL
#define RTOS_DEQUE_MARKER 0x42088024UL

#define osDynamicAlloc 0x80000000U
#define osReapThread 0x40000000UL

#define RTOS_NAME_SIZE 32UL
#define RTOS_DEFAULT_STACK_SIZE 1024UL
#define RTOS_TIMER_QUEUE_SIZE 5

#define osOnceFlagsInit 0

typedef uint32_t osResourceMarker_t;
typedef atomic_int osOnceFlag_t;
typedef osOnceFlag_t *osOnceFlagId_t;
typedef void (*osOnceFunc_t)(osOnceFlagId_t flag_id, void *context);

typedef struct linked_list *osResourceNode_t;
typedef void *osResource_t;
typedef osStatus_t (*osResouceNodeForEachFunc_t)(const osResource_t resource, void *context);

typedef void *osDequeId_t;

typedef enum
{
	osResourceThread = 0,
	osResourceMutex = 1,
	osResourceRobustMutex = 2,
	osResourceMemoryPool = 3,
	osResourceSemaphore = 4,
	osResourceEventFlags = 5,
	osResourceTimer = 6,
	osResourceMessageQueue = 7,
	osResourceDeque = 8,
	osResourceLast = 9,
	osResourceError = -1,
	osResourceReserved = 0x7fffffff,
} osResourceId_t;

typedef struct {
	const char *name;
	uint32_t attr_bits;
	void *cb_mem;
	uint32_t cb_size;
	void *dq_mem;
	uint32_t dq_size;
} osDequeAttr_t;

struct rtos_eventflags
{
	osResourceMarker_t marker;
	char name[RTOS_NAME_SIZE];

	uint32_t attr_bits;

	struct futex futex;

	atomic_long waiters;
	atomic_long flags;

	struct linked_list resource_node;
};

struct rtos_mutex
{
	osResourceMarker_t marker;
	char name[RTOS_NAME_SIZE];

	uint32_t attr_bits;

	struct futex futex;

	atomic_long value;
	int count;

	struct linked_list resource_node;
};

struct rtos_semaphore
{
	osResourceMarker_t marker;
	char name[RTOS_NAME_SIZE];

	uint32_t attr_bits;

	struct futex futex;

	uint32_t max_count;
	atomic_uint value;

	struct linked_list resource_node;
};

struct rtos_thread
{
	osResourceMarker_t marker;
	char name[RTOS_NAME_SIZE];

	uint32_t attr_bits;
	osThreadFunc_t func;
	void *context;
	void *stack;
	size_t stack_size;

	struct rtos_eventflags joiner;
	struct rtos_eventflags flags;

	struct linked_list resource_node;

	uint8_t stack_area[] __aligned(8);
};

struct rtos_memory_pool
{
	osResourceMarker_t marker;
	char name[RTOS_NAME_SIZE];

	uint32_t attr_bits;

	size_t block_size;
	size_t capacity;
	void *pool_data;

	struct rtos_semaphore pool_semaphore;
	void **free_list;
	spinlock_t lock;

	struct linked_list resource_node;

	uint32_t data[] __aligned(8);
};

struct rtos_timer
{
	osResourceMarker_t marker;
	char name[RTOS_NAME_SIZE];

	uint32_t attr_bits;

	osTimerType_t type;
	osTimerFunc_t func;
	void *argument;
	uint32_t ticks;

	uint32_t target;

	struct linked_list node;

	struct linked_list resource_node;
};

struct rtos_message
{
	uint32_t priority;
	struct linked_list node;

	char data[] __aligned(8);
};

struct rtos_message_queue
{
	osResourceMarker_t marker;
	char name[RTOS_NAME_SIZE];

	uint32_t attr_bits;

	size_t msg_size;
	size_t msg_count;

	struct rtos_semaphore data_available;
	struct rtos_memory_pool message_pool;
	spinlock_t lock;

	struct linked_list messages;

	struct linked_list resource_node;

	uint32_t data[] __aligned(8);
};

struct rtos_deque
{
	osResourceMarker_t marker;
	char name[RTOS_NAME_SIZE];

	uint32_t attr_bits;

	size_t element_size;
	size_t element_count;

	struct rtos_eventflags events;

	size_t front;
	size_t back;
	void *buffer;
	atomic_ulong waiters;
	spinlock_t lock;

	struct linked_list resource_node;

	uint32_t data[] __aligned(8);
};

struct rtos_resource
{
	osResourceMarker_t marker;
	char name[RTOS_NAME_SIZE];

	size_t offset;
	struct linked_list resource_list;
	spinlock_t lock;
};

struct rtos_kernel
{
	osResourceMarker_t marker;

	osKernelState_t state;
	struct scheduler scheduler;

	int32_t locked;
	spinlock_t lock;

	struct rtos_resource resources[osResourceLast];
};

extern struct rtos_kernel *rtos2_kernel;
extern const size_t osThreadMinimumStackSize;

osDequeId_t osDequeNew(uint32_t element_count, uint32_t element_size, const osDequeAttr_t *attr);
const char *osDequeGetName(osDequeId_t dq_id);
osStatus_t osDequePutFront(osDequeId_t dq_id, const void *element, uint32_t timeout);
osStatus_t osDequePutBack(osDequeId_t dq_id, const void *element, uint32_t timeout);
osStatus_t osDequeGetFront(osDequeId_t dq_id, void *element, uint32_t timeout);
osStatus_t osDequeGetBack(osDequeId_t dq_id, void *element, uint32_t timeout);
uint32_t osDequeGetCapacity(osDequeId_t dq_id);
uint32_t osDequeGetElementSize(osDequeId_t dq_id);
uint32_t osDequeGetCount(osDequeId_t dq_id);
uint32_t osDequeGetSpace(osDequeId_t dq_id);
osStatus_t osDequeReset(osDequeId_t dq_id);
osStatus_t osDequeDelete(osDequeId_t dq_id);

void osCallOnce(osOnceFlagId_t flag, osOnceFunc_t func, void *context);

osStatus_t osKernelResourceAdd(osResourceId_t resource_id, osResourceNode_t node);
osStatus_t osKernelResourceRemove(osResourceId_t resource_id, osResourceNode_t node);
bool osKernelResourceIsLocked(osResourceId_t resource_id);
osStatus_t osKernelResourceForEach(osResourceId_t resource_id, osResouceNodeForEachFunc_t func, void *context);
osStatus_t osKernelResourceDump(osResourceId_t resource_id);
osStatus_t osKernelResourceIsRegistered(osResourceId_t resource_id, osResource_t resource);

static inline __always_inline __optimize osStatus_t osKernelContextIsValid(bool allowed, uint32_t timeout)
{
	if (allowed) {

		uint32_t irq = __get_IPSR();
		if (irq == 0)
			return osOK;

		if (timeout != 0)
			return osErrorParameter;

		uint32_t irq_priority = NVIC_GetPriority(irq - 16);
		if (irq_priority < SCHEDULER_MAX_IRQ_PRIORITY)
			return osErrorISR;

		return osOK;

	} else if (__get_IPSR() != 0)
		return osErrorISR;

	return osOK;
}

static inline __always_inline __optimize osStatus_t osIsResourceValid(osResource_t resource, uint32_t marker)
{
	osResourceMarker_t *resource_marker = resource;
	if (resource_marker != 0 && (*resource_marker == marker || *resource_marker == ~marker))
		return osOK;

	/* Caller error or corruption  */
	return osErrorParameter;
}

static inline __always_inline __optimize uint32_t osSchedulerPriority(osPriority_t rtos2_priority)
{
	return SCHEDULER_MIN_TASK_PRIORITY - rtos2_priority;
}

static inline __always_inline __optimize osPriority_t osKernelPriority(uint32_t scheduler_priority)
{
	return SCHEDULER_MIN_TASK_PRIORITY - scheduler_priority;
}

osStatus_t osMemoryPoolIsBlockValid(osMemoryPoolId_t mp_id, void *block);
void osTimerTick(void);
osStatus_t osMutexRobustRelease(osMutexId_t mutex_id, osThreadId_t owner);

#endif
