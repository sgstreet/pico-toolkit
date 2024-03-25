/*
 * Copyright 2015 Stephen Street <stephen@redrocketcomputing.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef FAULT_H_
#define FAULT_H_

#define FAULT_HANDLER_STACK_SIZE 512
#define FAULT_BACKTRACE_SIZE 25

struct fault_frame
{
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
	uint32_t IP;
	uint32_t LR;
	uint32_t PC;
	uint32_t PSR;
};

struct callee_registers
{
	uint32_t r8;
	uint32_t r9;
	uint32_t r10;
	uint32_t r11;

	uint32_t r4;
	uint32_t r5;
	uint32_t r6;
	uint32_t r7;
};

struct cortexm_fault
{
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
	uint32_t r4;
	uint32_t r5;
	uint32_t r6;
	uint32_t r7;
	uint32_t r8;
	uint32_t r9;
	uint32_t r10;
	uint32_t r11;
	uint32_t IP;
	uint32_t LR;
	uint32_t SP;
	uint32_t PC;
	uint32_t PSR;
	uint32_t fault_type;
	uint32_t exception_return;
	uint32_t core;
};

extern void fault(const struct fault_frame *fault_frame, const struct callee_registers *callee_registers, uint32_t exception_return);
extern void hard_fault(const struct fault_frame *fault_frame, const struct callee_registers *callee_registers, uint32_t exception_return);

/* These function allow the application to dump, save and continue from a fault */
extern void init_fault(void);
extern void save_fault(const struct cortexm_fault *fault);
extern void reset_fault(const struct cortexm_fault *fault);

#endif
