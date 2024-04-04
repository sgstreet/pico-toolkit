/*
 * Copyright (C) 2021 Red Rocket Computing, LLC
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * fault.c
 *
 * Created on: Mar 27, 2021
 *     Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <pico/platform.h>

#include <pico/toolkit/compiler.h>
#include <pico/toolkit/cmsis.h>
#include <pico/toolkit/tls.h>
#include <pico/toolkit/fault.h>
#include <pico/toolkit/backtrace.h>

core_local void *fault_stack = 0;
core_local struct backtrace fault_backtrace[10];

__weak void init_fault()
{
}

__weak void save_fault(const struct cortexm_fault *fault)
{
	struct backtrace *backtrace = cls_datum(fault_backtrace);

	backtrace_frame_t backtrace_frame;
	uint32_t fault_pc = fault->exception_return == 0xfffffff1 ? fault->LR : fault->PC;

	/* Setup for a backtrace */
	backtrace_frame.fp = fault->r7;
	backtrace_frame.lr = fault->LR;
	backtrace_frame.sp = fault->SP;
	backtrace_frame.pc = fault_pc;

	/* I'm not convinced this is correct,  */
	backtrace_frame.pc = fault->exception_return == 0xfffffff1 ? fault->LR : fault->PC;

	/* Try the unwind */
	int backtrace_entries = _backtrace_unwind(backtrace, array_sizeof(fault_backtrace), &backtrace_frame);

	/* Print header */
	fprintf(stderr, "\ncore %u faulted at 0x%08x with PSR 0x%08x\n", fault->core, fault_pc, fault->PSR);

	/* Dump the registers first */
	fprintf(stderr, "\tr0:  0x%08x r1:  0x%08x r2:  0x%08x r3:  0x%08x\n", fault->r0, fault->r1, fault->r2, fault->r3);
	fprintf(stderr, "\tr4:  0x%08x r5:  0x%08x r6:  0x%08x r7:  0x%08x\n", fault->r4, fault->r5, fault->r6, fault->r7);
	fprintf(stderr, "\tr8:  0x%08x r9:  0x%08x r10: 0x%08x r11: 0x%08x\n", fault->r8, fault->r9, fault->r10, fault->r11);
	fprintf(stderr, "\tIP:  0x%08x LR:  0x%08x SP:  0x%08x PC:  0x%08x\n", fault->IP, fault->LR, fault->SP, fault->PC);

	/* Followed by the back trace */
	fprintf(stderr, "\nbacktrace:\n");
	for (size_t i = 0; i < backtrace_entries; ++i)
		fprintf(stderr, "\t%s@%p - %p\n", backtrace[i].name, backtrace[i].function, backtrace[i].address);
}

__weak __noreturn void reset_fault(const struct cortexm_fault *fault)
{
	abort();
}

static __isr_section void assemble_cortexm_fault(struct cortexm_fault *fault, const struct fault_frame *fault_frame, const struct callee_registers *callee_registers, uint32_t exception_return)
{
	/* Collect the fault information */
	fault->r0 = fault_frame->r0;
	fault->r1 = fault_frame->r1;
	fault->r2 = fault_frame->r2;
	fault->r3 = fault_frame->r3;
	fault->r4 = callee_registers->r4;
	fault->r5 = callee_registers->r5;
	fault->r6 = callee_registers->r6;
	fault->r7 = callee_registers->r7;
	fault->r8 = callee_registers->r8;
	fault->r9 = callee_registers->r9;
	fault->r10 = callee_registers->r10;
	fault->r11 = callee_registers->r11;

	fault->IP = fault_frame->IP;
	fault->SP = (uint32_t)fault_frame;
	fault->LR = fault_frame->LR;
	fault->PC = fault_frame->PC;
	fault->PSR = fault_frame->PSR;

	fault->fault_type = SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk;
	fault->exception_return = exception_return;
	fault->core = get_core_num();
}

__isr_section void hard_fault(const struct fault_frame *fault_frame, const struct callee_registers *callee_registers, uint32_t exception_return)
{
	struct cortexm_fault fault;

	/* Initialize fault handling */
	init_fault();

	/* Assemble the fault information */
	assemble_cortexm_fault(&fault, fault_frame, callee_registers, exception_return);

	/* Save the fault information */
	save_fault(&fault);

	/* Reset the fault */
	reset_fault(&fault);
}

static void fault_init(void)
{
	void *stack = sbrk(FAULT_HANDLER_STACK_SIZE);
	cls_datum(fault_stack) = stack + FAULT_HANDLER_STACK_SIZE;
}
PREINIT_WITH_PRIORITY(fault_init, 050);
