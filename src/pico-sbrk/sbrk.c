/*
 * sbrk.c
 *
 *  Created on: Apr 26, 2023
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <errno.h>
#include <stdint.h>
#include <unistd.h>

#include <sys/lock.h>

extern uintptr_t end;
extern uintptr_t __StackLimit;

static uintptr_t brk = (uintptr_t)&end;

void *sbrk(ptrdiff_t incr)
{
	uintptr_t block;

	/* Protect the heap pointer */
	__LIBC_LOCK();

	uintptr_t new_brk = brk + incr;
	if (new_brk >= (uintptr_t)&end || new_brk <= (uintptr_t)&__StackLimit) {
		block = brk;
		brk = new_brk;
	} else {
		errno = ENOMEM;
		block = -1;
	}

	/* Good or bad let the lock go */
	__LIBC_UNLOCK();

	/* Most like success */
	return (void *)block;
}

