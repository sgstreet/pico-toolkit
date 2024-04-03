/*
 * spinlock.h
 *
 *  Created on: Dec 26, 2022
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#ifndef _SPINLOCK_H_
#define _SPINLOCK_H_

#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>

#include <pico/toolkit/cmsis.h>

typedef atomic_ulong spinlock_t;

static inline void spin_lock(spinlock_t *spinlock)
{
	assert(spinlock != 0);

	uint16_t ticket = atomic_fetch_add(spinlock, 1UL << 16) >> 16;
	while ((*spinlock & 0xffff) != ticket)
		__WFE();
}

static inline unsigned int spin_lock_irqsave(spinlock_t *spinlock)
{
	assert(spinlock != 0);

	uint32_t state = disable_interrupts();
	spin_lock(spinlock);
	__SEV();
	return state;
}

static inline bool spin_try_lock(spinlock_t *spinlock)
{
	assert(spinlock != 0);

	uint32_t value = *spinlock;
	if ((value >> 16) != (value & 0xffff))
		return false;

	return atomic_compare_exchange_strong(spinlock, &value, value + (1UL << 16));
}

static inline bool spin_try_lock_irqsave(spinlock_t *spinlock, unsigned int *state)
{
	assert(spinlock != 0 && state != 0);

	uint32_t irq_state = disable_interrupts();
	bool locked = spin_try_lock(spinlock);
	if (!locked) {
		enable_interrupts(irq_state);
		return false;
	}

	*state = irq_state;
	return true;
}

static inline void spin_unlock(spinlock_t *spinlock)
{
	assert(spinlock != 0);
	atomic_fetch_add((uint16_t *)spinlock, 1);
}

static inline void spin_unlock_irqrestore(spinlock_t *spinlock, unsigned int state)
{
	assert(spinlock != 0);

	spin_unlock(spinlock);
	enable_interrupts(state);
}

#endif
