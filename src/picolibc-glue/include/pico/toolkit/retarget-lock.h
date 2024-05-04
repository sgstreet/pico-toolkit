/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * retarget-lock.h
 *
 *  Created on: Mar 19, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#ifndef _RETARGET_LOCK_H_
#define _RETARGET_LOCK_H_

#include <sys/lock.h>

#define LIBC_LOCK_MARKER 0x89988998

struct __retarget_runtime_lock
{
	long value;
	long expected;
	long count;
	bool allocated;
	unsigned int marker;
};

struct __lock
{
	void *retarget_lock;
};

extern void __retarget_runtime_lock_init_once(struct __lock *lock);
extern void __retarget_runtime_lock_init(_LOCK_T *lock);
extern void __retarget_runtime_lock_close(_LOCK_T lock);
extern int __retarget_runtime_lock_try_acquire(_LOCK_T lock);
extern void __retarget_runtime_lock_release(_LOCK_T lock);

extern long __retarget_runtime_lock_value(void);
extern void __retarget_runtime_relax(_LOCK_T lock);
extern void __retarget_runtime_wake(_LOCK_T lock);

#endif
