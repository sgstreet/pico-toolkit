/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * pico-atomic.c
 *
 *  Created on: Mar 14, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 *
 */

#include <stdbool.h>
#include <stdint.h>
#include <pico/toolkit/compiler.h>

#include "hardware/address_mapped.h"
#include "hardware/regs/watchdog.h"
#include "hardware/regs/sio.h"
#include "hardware/sync.h"

#include "pico/config.h"

/* Must be powers of 2 */
#define ATOMIC_STRIPE 4UL
#define ATOMIC_LOCKS 16UL
#define ATOMIC_LOCK_WIDTH 2UL
#define ATOMIC_LOCK_IDX_Pos ((sizeof(unsigned long) * 8) - (__builtin_clz(ATOMIC_STRIPE - 1)))
#define ATOMIC_LOCK_IDX_Msk (ATOMIC_LOCKS - 1UL)
#define ATOMIC_LOCK_REG ((io_rw_32 *)(WATCHDOG_BASE + WATCHDOG_SCRATCH3_OFFSET))

static void __atomic_init(void)
{
	*ATOMIC_LOCK_REG = 0;
}
PREINIT_WITH_PRIORITY(__atomic_init, 030);

/* 
	To eliminate interference with existing hardware spinlock usage and reduce multicore contention on
	unique atomic variables, we use one of the watchdog scratch registers (WATCHDOG_SCRATCH3) to
	implement 16, 2 bit, multicore locks, via a varation of Peterson's algorithm
	(see https://en.wikipedia.org/wiki/Dekker%27s_algorithm). The lock is selected as a
	function of the variable address and the stripe width which hashes variables
	addresses to locks numbers.
 */
static __optimize uint32_t __atomic_lock(const volatile void *mem)
{
	const uint32_t core = get_core_num();
	const uint32_t lock_idx = (((uintptr_t)mem) >> ATOMIC_LOCK_IDX_Pos) & ATOMIC_LOCK_IDX_Msk;
	const uint32_t lock_pos = lock_idx * ATOMIC_LOCK_WIDTH;
	const uint32_t lock_mask = ((1UL << ATOMIC_LOCK_WIDTH) - 1) << lock_pos;
	const uint32_t locked_mask = 1UL << (lock_pos + core);

	uint32_t state = save_and_disable_interrupts();
	while(true) {

		/* First set the bit */
		hw_set_bits(ATOMIC_LOCK_REG, locked_mask);
		__dmb();

		/* Did we get the lock? */
		if((*ATOMIC_LOCK_REG & lock_mask) == locked_mask)
			break;

		/* Nope, clear our side */
		__dmb();
		hw_clear_bits(ATOMIC_LOCK_REG, locked_mask);

		/* Need to break any ties if the cores are in lock step, is this really required? */
		for(uint32_t i = core * 2; i > 0; --i)
			asm volatile ("nop");
	}

	return state;
}

static __optimize void __atomic_unlock(const volatile void *mem, uint32_t state)
{
	const uint32_t lock_idx = (((uintptr_t)mem) >> ATOMIC_LOCK_IDX_Pos) & ATOMIC_LOCK_IDX_Msk;
	const uint32_t lock_pos = lock_idx * ATOMIC_LOCK_WIDTH;
	const uint32_t locked_mask = 1UL << (lock_pos + get_core_num());

	__dmb();
	hw_clear_bits(ATOMIC_LOCK_REG, locked_mask);
	restore_interrupts(state);
}

__optimize uint8_t __atomic_fetch_add_1(volatile void *mem, uint8_t val, int model)
{
	volatile uint8_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint8_t result = *ptr;
	*ptr += val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint8_t __atomic_fetch_sub_1(volatile void *mem, uint8_t val, int model)
{
	volatile uint8_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint8_t result = *ptr;
	*ptr -= val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint8_t __atomic_fetch_and_1(volatile void *mem, uint8_t val, int model)
{
	volatile uint8_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint8_t result = *ptr;
	*ptr &= val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint8_t __atomic_fetch_or_1(volatile void *mem, uint8_t val, int model)
{
	volatile uint8_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint8_t result = *ptr;
	*ptr |= val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint8_t __atomic_exchange_1(volatile void *mem, uint8_t val, int model)
{
	volatile uint8_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint8_t result = *ptr;
	*ptr = val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize bool __atomic_compare_exchange_1(volatile void *mem, void *expected, uint8_t desired, bool weak, int success, int failure)
{
	bool result = false;
	volatile uint8_t *ptr = mem;
	uint8_t *e_ptr = expected;
	uint32_t state = __atomic_lock(mem);
	if(*ptr == *e_ptr) {
		*ptr = desired;
		result = true;
	} else
		*e_ptr = *ptr;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint16_t __atomic_fetch_add_2(volatile void *mem, uint16_t val, int model)
{
	volatile uint16_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint16_t result = *ptr;
	*ptr += val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint16_t __atomic_fetch_sub_2(volatile void *mem, uint16_t val, int model)
{
	volatile uint16_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint16_t result = *ptr;
	*ptr -= val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint16_t __atomic_fetch_and_2(volatile void *mem, uint16_t val, int model)
{
	volatile uint16_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint16_t result = *ptr;
	*ptr &= val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint16_t __atomic_fetch_or_2(volatile void *mem, uint16_t val, int model)
{
	volatile uint16_t *ptr = mem;
	uint16_t state = __atomic_lock(mem);
	uint16_t result = *ptr;
	*ptr |= val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint16_t __atomic_exchange_2(volatile void *mem, uint16_t val, int model)
{
	volatile uint16_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint16_t result = *ptr;
	*ptr = val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize bool __atomic_compare_exchange_2(volatile void *mem, void *expected, uint16_t desired, bool weak, int success, int failure)
{
	bool result = false;
	volatile uint16_t *ptr = mem;
	uint16_t *e_ptr = expected;
	uint32_t state = __atomic_lock(mem);
	if(*ptr == *e_ptr) {
		*ptr = desired;
		result = true;
	} else
		*e_ptr = *ptr;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint32_t __atomic_fetch_add_4(volatile void *mem, uint32_t val, int model)
{
	volatile uint32_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint32_t result = *ptr;
	*ptr += val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint32_t __atomic_fetch_sub_4(volatile void *mem, uint32_t val, int model)
{
	volatile uint32_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint32_t result = *ptr;
	*ptr -= val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint32_t __atomic_fetch_and_4(volatile void *mem, uint32_t val, int model)
{
	volatile uint32_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint32_t result = *ptr;
	*ptr &= val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint32_t __atomic_fetch_or_4(volatile void *mem, uint32_t val, int model)
{
	volatile uint32_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint32_t result = *ptr;
	*ptr |= val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint32_t __atomic_exchange_4(volatile void *mem, uint32_t val, int model)
{
	volatile uint32_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint32_t result = *ptr;
	*ptr = val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize bool __atomic_compare_exchange_4(volatile void *mem, void *expected, uint32_t desired, bool weak, int success, int failure)
{
	bool result = false;
	volatile uint32_t *ptr = mem;
	uint32_t *e_ptr = expected;
	uint32_t state = __atomic_lock(mem);
	if(*ptr == *e_ptr) {
		*ptr = desired;
		result = true;
	} else
		*e_ptr = *ptr;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint64_t __atomic_fetch_add_8(volatile void *mem, uint64_t val, int model)
{
	volatile uint64_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint64_t result = *ptr;
	*ptr += val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint64_t __atomic_fetch_sub_8(volatile void *mem, uint64_t val, int model)
{
	volatile uint64_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint64_t result = *ptr;
	*ptr -= val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint64_t __atomic_fetch_and_8(volatile void *mem, uint64_t val, int model)
{
	volatile uint64_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint64_t result = *ptr;
	*ptr &= val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint64_t __atomic_fetch_or_8(volatile void *mem, uint64_t val, int model)
{
	volatile uint64_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint64_t result = *ptr;
	*ptr |= val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint64_t __atomic_exchange_8(volatile void *mem, uint64_t val, int model)
{
	volatile uint64_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint64_t result = *ptr;
	*ptr = val;
	__atomic_unlock(mem, state);
	return result;
}

__optimize bool __atomic_compare_exchange_8(volatile void *mem, void *expected, uint64_t desired, bool weak, int success, int failure)
{
	bool result = false;
	volatile uint64_t *ptr = mem;
	uint64_t *e_ptr = expected;
	uint32_t state = __atomic_lock(mem);
	if(*ptr == *e_ptr) {
		*ptr = desired;
		result = true;
	} else
		*e_ptr = *ptr;
	__atomic_unlock(mem, state);
	return result;
}

__optimize uint64_t __atomic_load_8(const volatile void *mem, int model)
{
	const volatile uint64_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	uint32_t result = *ptr;
	__atomic_unlock(mem, state);
	return result;
}

__optimize void __atomic_store_8(volatile void *mem, uint64_t val, int model)
{
	volatile uint64_t *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	*ptr = val;
	__atomic_unlock(mem, state);
}

__optimize bool __atomic_test_and_set_m0(volatile void *mem, int model)
{
	volatile bool *ptr = mem;
	uint32_t state = __atomic_lock(mem);
	volatile bool result = *ptr;
	*ptr = true;
	__atomic_unlock(mem, state);
	return result;
}

__optimize void __atomic_clear_m0(volatile void *mem, int model)
{
	volatile bool *ptr = mem;
	*ptr = false;
	__dmb();
}
