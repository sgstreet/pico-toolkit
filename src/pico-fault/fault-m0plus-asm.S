/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * fault-m7-asm.S
 *
 * Created on: Mar 27, 2021
 *     Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <pico/toolkit/asm.h>

declare_function HardFault_Handler, .text
	.fnstart
	.cantunwind

	/* We need the address of the core local data we need to do this because we may not have a valid stack */
	ldr        r0, =0xd0000000
	ldr        r0, [r0, #0]
	cmp        r0, #0
	beq        0f
	ldr        r0, =__core_1
	b          1f
0:
	ldr        r0, =__core_0
1:

	/* Now calculate the offset to the fault stack pointer */
	ldr        r1, =fault_stack
	ldr        r2, =__core_data
	subs       r1, r1, r2

	/* This is the final memory address of the fault stack on a per core basis */
	ldr        r2, [r0, r1]

	/* Pass pointer to exception frame in R0, this is the stack pointer */
	movs       r0, #4
	mov        r1, lr
	tst        r0, r1        /* Test EXEC_RETURN for used stack */
	beq        2f
	mrs        r0, psp       /* Move the process stack poiner to R0 */
	b          3f
2:
	mrs        r0, msp       /* Move the main stack pointer to R0 */
3:

	# Now move to the good stack pointer
	mov        sp, r2

	/* Assemble callee saved registers and pass pointer in R1, need to push callee saved register */
	push       {r4-r7}
	mov        r4, r8
	mov        r5, r9
	mov        r6, r10
	mov        r7, r11
	push       {r4-r7}

	/* Pass the callee saved registers in R1 */
	mov        r1, sp

	/* Pass EXEC_RETURN in r2 */
	mov        r2, lr

	/* Invoke the backend handler, which should never return */
	ldr			r3, =hard_fault
	bx			r3

	.fnend
	.pool
	.size	HardFault_Handler, . - HardFault_Handler
