/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * svc.h
 *
 * Created on: Dec 30, 2022
 *     Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#ifndef _SVC_H_
#define _SVC_H_

struct svc_frame
{
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
};

static inline __always_inline int svc_call0(const uint8_t code)
{
	int result;

	asm volatile
	(
		"svc 	%[code]             \n\t"
		"mov	%[result], r0       \n\t"
		: [result] "=r" (result) : [code] "I" (code)
	);

	return result;
}

static inline __always_inline int svc_call1(const uint8_t code, uint32_t arg0)
{
	int result;

	asm volatile
	(
		"mov	r0, %[arg0]         \n\t"
		"svc 	%[code]             \n\t"
		"mov	%[result], r0        \n\t"
		: [result] "=r" (result) : [code] "I" (code), [arg0] "r" (arg0) : "r0"
	);

	return result;
}

static inline __always_inline int svc_call2(const uint8_t code, uint32_t arg0, uint32_t arg1)
{
	int result;

	asm volatile
	(
		"mov	r0, %[arg0]         \n\t"
		"mov	r1, %[arg1]         \n\t"
		"svc 	%[code]             \n\t"
		"mov	%[result], r0       \n\t"
		: [result] "=r" (result) : [code] "I" (code), [arg0] "r" (arg0), [arg1] "r" (arg1) : "r0", "r1"
	);

	return result;
}

static inline __always_inline int svc_call3(const uint8_t code, uint32_t arg0, uint32_t arg1, uint32_t arg2)
{
	int result;

	asm volatile
	(
		"mov	r0, %[arg0]         \n\t"
		"mov	r1, %[arg1]         \n\t"
		"mov	r2, %[arg2]         \n\t"
		"svc 	%[code]             \n\t"
		"mov	%[result], r0       \n\t"
		: [result] "=r" (result) : [code] "I" (code), [arg0] "r" (arg0), [arg1] "r" (arg1), [arg2] "r" (arg2) : "r0", "r1", "r2"
	);

	return result;
}

static inline __always_inline int svc_call4(const uint8_t code, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
	int result;

	asm volatile
	(
		"mov	r0, %[arg0]         \n\t"
		"mov	r1, %[arg1]         \n\t"
		"mov	r2, %[arg2]         \n\t"
		"mov	r3, %[arg3]         \n\t"
		"svc 	%[code]             \n\t"
		"mov	%[result], r0       \n\t"
		: [result] "=r" (result) : [code] "I" (code), [arg0] "r" (arg0), [arg1] "r" (arg1), [arg2] "r" (arg2), [arg3] "r" (arg3): "r0", "r1", "r2", "r3"
	);

	return result;
}

#endif
