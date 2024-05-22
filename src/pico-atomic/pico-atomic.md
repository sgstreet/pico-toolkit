# Pico Atomic

## Detailed Description

The Cotex-M0+ libc runtime does not provide an implementation of the [ISO C11 Atomic Operations](https://en.cppreference.com/w/c/thread) because the Armv6-M architecture does not offer the LDREX/STREX functions needed to implement lockless atomic operations.  IMHO, this is a mistake that precludes the creation of portable, perfomant data structures and algorithms across the Arm Cortex-M0/3/4/7 family of micro-controllers.  This becomes an acute issue on the RP2040 when coordinating across multiple cores.

The pico-atomic library implements the required GCC runtime functions for 1, 2, 4, and 8-byte atomic operations.  While these operations are not lockless, the implementation takes pains to minimize the duration of interrupt masking and multicore spin lock ownership.

To eliminate interference with existing RP2040 hardware spinlock usage while reducing multicore contention to unique atomic variables, the library uses one of the watchdog scratch registers (WATCHDOG_SCRATCH3) to implement 16, 2-bit, multicore locks, via a variation of [Peterson's algorithm](https://en.wikipedia.org/wiki/Dekker%27s_algorithm). The lock is selected as a function of the variable address and the stripe width which hashes atomic variable addresses to one of 16 locks.

<sub>Example</sub>
```
static inline void spin_lock(spinlock_t *spinlock)
{
	assert(spinlock != 0);

	uint16_t ticket = atomic_fetch_add(spinlock, 1UL << 16) >> 16;
	while ((*spinlock & 0xffff) != ticket)
		__WFE();
}

static inline void spin_unlock(spinlock_t *spinlock)
{
	assert(spinlock != 0);
	atomic_fetch_add((uint16_t *)spinlock, 1);
	__SEV();
}
```

## Functions

See https://en.cppreference.com/w/c/thread
