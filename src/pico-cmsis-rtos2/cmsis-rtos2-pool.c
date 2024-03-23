#include <stdlib.h>
#include <string.h>

#include <pico/toolkit/compiler.h>

#include <cmsis/cmsis-rtos2.h>

extern void *_rtos2_alloc(size_t size);
extern void _rtos2_release(void *ptr);

extern __weak void *_rtos2_pool_alloc(size_t pool_size);
extern __weak void _rtos2_pool_release(void *ptr);

__weak void *_rtos2_pool_alloc(size_t pool_size)
{
	return _rtos2_alloc(sizeof(struct rtos_memory_pool) + pool_size);
}

__weak void _rtos2_pool_release(void *ptr)
{
	_rtos2_release(ptr);
}

osMemoryPoolId_t osMemoryPoolNew (uint32_t block_count, uint32_t block_size, const osMemoryPoolAttr_t *attr)
{
	const osMemoryPoolAttr_t default_attr = { .name = "" };

	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Fail if the block size is not at multiple of 8 bytes, this will aligned access for 32 bit integers */
	if (block_size != ((block_size + 3) & ~3))
		return 0;

	/* Check for attribute */
	if (!attr)
		attr = &default_attr;

	/* Setup the pool memory */
	struct rtos_memory_pool *new_pool = 0;
	if (!attr->cb_mem && !attr->mp_mem) {

		/* Dynamic allocation */
		new_pool = _rtos2_pool_alloc(block_size * block_count);
		if (!new_pool)
			return 0;

		/* Initialize the pointers */
		new_pool->pool_data = new_pool->data;
		new_pool->attr_bits = attr->attr_bits | osDynamicAlloc;

	/* Static allocation */
	} else if (attr->cb_mem && attr->mp_mem) {

		if (attr->cb_size < sizeof(struct rtos_memory_pool) || attr->mp_size < block_size * block_count)
			return 0;

		/* Initialize the pointers */
		new_pool = attr->cb_mem;
		new_pool->pool_data = attr->mp_mem;

	/* Static memory allocation is all or nothing */
	} else
		return 0;

	/* Initialize the control block */
	new_pool->marker = RTOS_MEMORY_POOL_MARKER;
	strncpy(new_pool->name, (attr->name == 0 ? default_attr.name : attr->name), RTOS_NAME_SIZE);
	new_pool->name[RTOS_NAME_SIZE - 1] = 0;
	new_pool->block_size = block_size;
	new_pool->capacity = block_count;
	new_pool->lock = 0;

	/* Initialize the semaphore */
	osSemaphoreAttr_t semaphore_attr = { .name = attr->name, .cb_mem = &new_pool->pool_semaphore, .cb_size = sizeof(struct rtos_semaphore) };
	if (osSemaphoreNew(block_count, block_count, &semaphore_attr) == 0) {
		if (new_pool->attr_bits & osDynamicAlloc)
			_rtos2_pool_release(new_pool);
		return 0;
	}

	/* Initialize the free list */
	new_pool->free_list = 0;
	for (int i = 0; i < block_count; ++i) {
		void **block = new_pool->pool_data + (i * block_size);
		*block = new_pool->free_list;
		new_pool->free_list = block;
	}

	/* Add to the kernel resources */
	if (osKernelResourceAdd(osResourceMemoryPool, &new_pool->resource_node) != osOK) {
		osSemaphoreDelete(&new_pool->pool_semaphore);
		if (new_pool->attr_bits & osDynamicAlloc)
			_rtos2_pool_release(new_pool);
		return 0;
	}

	/* Wow, success */
	return new_pool;
}

const char *osMemoryPoolGetName (osMemoryPoolId_t mp_id)
{
	/* This would be bad */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the pool */
	os_status = osIsResourceValid(mp_id, RTOS_MEMORY_POOL_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_memory_pool *pool = mp_id;

	/* Lots of work for a pointer */
	return strlen(pool->name) > 0 ? pool->name : 0;
}

void *osMemoryPoolAlloc (osMemoryPoolId_t mp_id, uint32_t timeout)
{
	/* Validate the context */
	osStatus_t os_status = osKernelContextIsValid(true, timeout);
	if (os_status != osOK)
		return 0;

	/* Validate the pool */
	os_status = osIsResourceValid(mp_id, RTOS_MEMORY_POOL_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_memory_pool *pool = mp_id;

	/* Acquire a token from the semaphore */
	os_status = osSemaphoreAcquire(&pool->pool_semaphore, timeout);
	if (os_status != osOK)
		return 0;

	/* There must be a block available, get it */
	uint32_t state = spin_lock_irqsave(&pool->lock);
	void **block = pool->free_list;
	pool->free_list = *block;
	spin_unlock_irqrestore(&pool->lock, state);

	/* Should be good */
	return block;
}

osStatus_t osMemoryPoolFree (osMemoryPoolId_t mp_id, void *block)
{
	/* Validate the context */
	osStatus_t os_status = osKernelContextIsValid(true, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the pool */
	os_status = osIsResourceValid(mp_id, RTOS_MEMORY_POOL_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_memory_pool *pool = mp_id;

	/* Was a block provided and is it in the expected range */
	if (block < pool->pool_data || block > pool->pool_data + (pool->block_size * pool->capacity))
		return osErrorParameter;

	/* Add the block to the free list */
	uint32_t state = spin_lock_irqsave(&pool->lock);
	*(void **)block = pool->free_list;
	pool->free_list = block;
	spin_unlock_irqrestore(&pool->lock, state);

	/* Release the token from the semaphore */
	os_status = osSemaphoreRelease(&pool->pool_semaphore);
	if (os_status != osOK) {
		uint32_t state = spin_lock_irqsave(&pool->lock);
		list_remove(block);
		spin_unlock_irqrestore(&pool->lock, state);
		return os_status;
	}

	return osOK;
}

uint32_t osMemoryPoolGetCapacity (osMemoryPoolId_t mp_id)
{
	/* Validate the context */
	osStatus_t os_status = osKernelContextIsValid(true, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the pool */
	os_status = osIsResourceValid(mp_id, RTOS_MEMORY_POOL_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_memory_pool *pool = mp_id;

	/* Return it */
	return pool->capacity;
}

uint32_t osMemoryPoolGetBlockSize (osMemoryPoolId_t mp_id)
{
	/* Validate the context */
	osStatus_t os_status = osKernelContextIsValid(true, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the pool */
	os_status = osIsResourceValid(mp_id, RTOS_MEMORY_POOL_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_memory_pool *pool = mp_id;

	/* Return it */
	return pool->block_size;
}

uint32_t osMemoryPoolGetCount (osMemoryPoolId_t mp_id)
{
	/* Validate the context */
	osStatus_t os_status = osKernelContextIsValid(true, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the pool */
	os_status = osIsResourceValid(mp_id, RTOS_MEMORY_POOL_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_memory_pool *pool = mp_id;

	/* Return it */
	return pool->capacity - osSemaphoreGetCount(&pool->pool_semaphore);
}

uint32_t osMemoryPoolGetSpace (osMemoryPoolId_t mp_id)
{
	/* Validate the context */
	osStatus_t os_status = osKernelContextIsValid(true, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the pool */
	os_status = osIsResourceValid(mp_id, RTOS_MEMORY_POOL_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_memory_pool *pool = mp_id;

	/* Return it */
	return osSemaphoreGetCount(&pool->pool_semaphore);
}

osStatus_t osMemoryPoolIsBlockValid(osMemoryPoolId_t mp_id, void *block)
{
	/* Validate the context */
	osStatus_t os_status = osKernelContextIsValid(true, 0);
	if (os_status != osOK)
		return 0;

	/* Validate the pool */
	os_status = osIsResourceValid(mp_id, RTOS_MEMORY_POOL_MARKER);
	if (os_status != osOK)
		return 0;
	struct rtos_memory_pool *pool = mp_id;

	/* Was a block provided and is it in the expected range */
	if (block < pool->pool_data || block > pool->pool_data + (pool->block_size * pool->capacity))
		return osErrorParameter;
	else
		return osOK;
}

osStatus_t osMemoryPoolDelete (osMemoryPoolId_t mp_id)
{
	/* Validate the context */
	osStatus_t os_status = osKernelContextIsValid(false, 0);
	if (os_status != osOK)
		return os_status;

	/* Validate the pool */
	os_status = osIsResourceValid(mp_id, RTOS_MEMORY_POOL_MARKER);
	if (os_status != osOK)
		return os_status;
	struct rtos_memory_pool *pool = mp_id;

	/* Clear the marker */
	pool->marker = 0;

	/* Remove the resource */
	os_status = osKernelResourceRemove(osResourceMemoryPool, &pool->resource_node);
	if (os_status != osOK)
		return os_status;

	/* Release the semphore */
	os_status = osSemaphoreDelete(&pool->pool_semaphore);
	if (os_status != osOK)
		return os_status;

	/* Release any memory */
	if (pool->attr_bits & osDynamicAlloc)
		_rtos2_pool_release(pool);

	/* All good */
	return osOK;
}
