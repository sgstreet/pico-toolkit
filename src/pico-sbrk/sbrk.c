/*
 * sbrk.c
 *
 *  Created on: Apr 26, 2023
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <errno.h>
#include <stdint.h>
#include <stdatomic.h>
#include <unistd.h>

extern uintptr_t end;
extern uintptr_t __StackLimit;

static atomic_uintptr_t brk = (uintptr_t)&end;

void *sbrk(ptrdiff_t incr)
{
	uintptr_t block = atomic_fetch_add(&brk, incr);

	if (incr < 0) {
		if (block - (uintptr_t)&__StackLimit < -incr) {
			atomic_fetch_sub(&brk, incr);
			errno = ENOMEM;
			return (void *)-1;
		}
	} else {
		if ((uintptr_t)&__StackLimit - block < incr) {
			atomic_fetch_sub(&brk, incr);
			errno = ENOMEM;
			return (void *)-1;
		}
	}

	return (void *)block;
}

