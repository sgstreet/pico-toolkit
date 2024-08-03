/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
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

static uintptr_t current_brk = (uintptr_t)&end;

void *sbrk(ptrdiff_t incr)
{
	uintptr_t block;

	/* Protect the heap pointer */
	__LIBC_LOCK();

	/* Move to new break, ensuring that the break pointer is always align on a 8 byte boundary */
	uintptr_t new_brk = current_brk + ((incr + 7) & ~7);

	if (new_brk >= (uintptr_t)&end || new_brk <= (uintptr_t)&__StackLimit) {
		block = current_brk;
		current_brk = new_brk;
	} else {
		errno = ENOMEM;
		block = -1;
	}

	/* Good or bad let the lock go */
	__LIBC_UNLOCK();

	/* Most like success */
	return (void *)block;
}

