/*
 * Copyright (C) 2022 Stephen Street
 *
 * preemt-sched-asm.S
 *
 * Created on: Feb 9, 2017
 *     Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <pico/toolkit/asm.h>

declare_function SVC_Handler_0, .text
	.fnstart

	/* TODO FIXME I'm sure we need to adjust the stack pointer by 40 bytes for the scheduler frame. See scheduler_svc_handler */
	/* Get the active stack pointer into r0 we should always be on the msp */
	mov r0, sp

	/* Save the current context */
	mov r2, lr               /* Save the exec return */
	mrs r3, control          /* Rember the control (mostly privilge state) */
	subs r0, r0, #40         /* Reserve space for the remainder of the scheduler frame */
	stmia r0!, {r2-r7}       /* Save the low registers */
	mov r4, r8
	mov r5, r9
	mov r6, r10
	mov r7, r11
	stmia r0!, {r4-r7}       /* Save the high registers */
	subs r0, r0, #40         /* Make r0 point at the start of the scheduler frame */

	/* Update the stack pointer */
	mov sp, r0

	/* First load the core location store pointer for this core */
	ldr r0, =__aeabi_read_cls
	blx r0

	/* Now calculate the offset to the scheduler initial frame */
	ldr r1, =scheduler_initial_frame
	ldr r3, =__core_data
	subs r1, r1, r3

	/* This is the final memory address of the scheduler initial frame on a per core basis */
	add r0, r0, r1

	/* Final save the scheduler frame */
	mov r1, sp
	str r1, [r0, #0]

	/* Trigger the PendSV */
	ldr r1, =0xe000ed04
	ldr r3, =0x10000000
	str r3, [r1]
	dsb

	/* We should go into the scheduler and only return on scheduler exit */
	bx r2

	.fnend
	.pool
	.size SVC_Handler_0, . - SVC_Handler_0

/* This is used for the scheduler service handlers and can optionaly create a complete scheduler frame, not just a exception frame */
declare_function scheduler_svc_handler, .text
	.fnstart

	/* First load the scheduler service handler address */
	lsls r2, r0, #2          /* Convert service number into table offset */
	mov r0, r1               /* Move the expection frame to r0 for passing to the scheduler */

	ldr r1, =scheduler_svc_vector /* Load the address of the service table */
	ldr r1, [r1, r2]         /* Load the service handler address */

	/* Do we need a scheduler frame */
	movs r2, #2              /* Check to see is bit 2 is set, indicating we need a scheduler frame */
	tst r1, r2
	bne 0f                   /* We need a scheduler frame */

	/* Forward to the handler who will do the return */
	bx r1

0:
	bics r1, r2              /* Clear the scheduler frame request bit */

	/* Adjust the stack point for frame is the caller was using the MSP */
	movs       r3, #4
	mov        r2, lr        /* This will also save the exec return */
	tst        r3, r2        /* Test EXEC_RETURN for used stack */
	bne        1f
	sub        sp, #40
1:

	/* Save the current context */
	mrs r3, control          /* Rember the control (mostly privilge state) */
	subs r0, r0, #40         /* Reserve space for the remainder of the scheduler frame */
	stmia r0!, {r2-r7}       /* Save the low registers */
	mov r4, r8
	mov r5, r9
	mov r6, r10
	mov r7, r11
	stmia r0!, {r4-r7}       /* Save the high registers */
	subs r0, r0, #40         /* Make r1 point at the start of the scheduler frame */

	/* Forward to the handler who will return */
	bx r1

	.fnend
	.pool
	.size scheduler_svc_handler, . - scheduler_svc_handler

function_alias SVC_Handler_1, scheduler_svc_handler
function_alias SVC_Handler_2, scheduler_svc_handler
function_alias SVC_Handler_3, scheduler_svc_handler
function_alias SVC_Handler_4, scheduler_svc_handler
function_alias SVC_Handler_5, scheduler_svc_handler
function_alias SVC_Handler_6, scheduler_svc_handler
function_alias SVC_Handler_7, scheduler_svc_handler
function_alias SVC_Handler_8, scheduler_svc_handler

declare_function PendSV_Handler, .text
	.fnstart

	/* First load the core location store pointer for this core, does not modify anything but r0 */
	mov r1, lr
	ldr r0, =__aeabi_read_cls
	blx r0
	mov lr, r1

	/* Now calculate the offset of the current task */
	ldr r1, =current_task
	ldr r2, =__core_data
	subs r1, r1, r2

	/* The final memory address R0 + R1 is the active core's current task, load the contents */
	ldr r0, [r0, r1]

	/* Skip creating a scheduler frame is the current task is null */
	cmp r0, #0
	beq	2f

	/* Get the active stack pointer into r0 */
	movs       r0, #4
	mov        r2, lr        /* Save the exec return */
	tst        r0, r2        /* Test EXEC_RETURN for used stack */
	beq        0f
	mrs        r0, psp       /* Move the process stack poiner to R0 */
	b          1f
0:
	mrs        r0, msp       /* Move the main stack pointer to R0 */
	sub        sp, #40
1:

	/* Save the current context */
	mrs r3, control          /* Rember the control (mostly privilge state) */
	subs r0, r0, #40         /* Reserve space for the remainder of the scheduler frame */
	stmia r0!, {r2-r7}       /* Save the low registers */
	mov r4, r8
	mov r5, r9
	mov r6, r10
	mov r7, r11
	stmia r0!, {r4-r7}       /* Save the high registers */
	subs r0, r0, #40         /* Make r0 point at the start of the scheduler frame */

2:
	/* Switch task */
	ldr r1, =scheduler_switch
	blx r1

	/* Load the new context, which was stashed */
	adds r0, r0, #24         /* Move to the start of the high regs */
	ldmia r0!, {r4-r7}       /* Restore the high regs */
	mov r8, r4
	mov r9, r5
	mov r10, r6
	mov r11, r7
	subs r0, r0, #40         /* Move to start of low registers, include the exec return and control */
	ldmia r0!, {r2-r7}       /* Restore them */
	adds r0, #16             /* Skip the high regs, we have already restored them above */

	/* Restore the control register */
	msr control, r3

	/* Set current stack pointer */
	movs r1, #4
	tst r1, r2               /* Check bit 3 of the exec return to determine the stack being used */
	bne 3f
	msr msp, r0              /* Must have be the initial start frame */
	b 4f
3:
	msr psp, r0              /* Must have a thread */
4:

	/* Let it fly */
	isb
	bx r2

	.fnend
	.pool
	.size PendSV_Handler, . - PendSV_Handler
