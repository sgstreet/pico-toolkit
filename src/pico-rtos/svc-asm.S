/*
 * Copyright (C) 2021 Red Rocket Computing, LLC
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

	.section .data.__svc_vector
	.align 2
	.globl __svc_vector
__svc_vector:
	.word SVC_Handler_0
	.word SVC_Handler_1
	.word SVC_Handler_2
	.word SVC_Handler_3
	.word SVC_Handler_4
	.word SVC_Handler_5
	.word SVC_Handler_6
	.word SVC_Handler_7
	.word SVC_Handler_8
	.word SVC_Handler_9
	.word SVC_Handler_10
	.word SVC_Handler_11
	.word SVC_Handler_12
	.word SVC_Handler_13
	.word SVC_Handler_14
	.word SVC_Handler_15
	.word SVC_Handler_16
	.word SVC_Handler_17
	.word SVC_Handler_18
	.word SVC_Handler_19
	.word SVC_Handler_20
	.word SVC_Handler_21
	.word SVC_Handler_22
	.word SVC_Handler_23
	.word SVC_Handler_24
	.word SVC_Handler_25
	.word SVC_Handler_26
	.word SVC_Handler_27
	.word SVC_Handler_28
	.word SVC_Handler_29
	.word SVC_Handler_30
	.word SVC_Handler_31
	.size	__svc_vector, . - __svc_vector

declare_weak_function default_service_handler, .text
	bx lr
	.pool
	.size default_service_handler, . - default_service_handler

.macro def_service_handler handler_name
	.weak \handler_name
	.thumb_set \handler_name, default_service_handler
.endm

def_service_handler SVC_Handler_0
def_service_handler SVC_Handler_1
def_service_handler SVC_Handler_2
def_service_handler SVC_Handler_3
def_service_handler SVC_Handler_4
def_service_handler SVC_Handler_5
def_service_handler SVC_Handler_6
def_service_handler SVC_Handler_7
def_service_handler SVC_Handler_8
def_service_handler SVC_Handler_9
def_service_handler SVC_Handler_10
def_service_handler SVC_Handler_11
def_service_handler SVC_Handler_12
def_service_handler SVC_Handler_13
def_service_handler SVC_Handler_14
def_service_handler SVC_Handler_15
def_service_handler SVC_Handler_16
def_service_handler SVC_Handler_17
def_service_handler SVC_Handler_18
def_service_handler SVC_Handler_19
def_service_handler SVC_Handler_20
def_service_handler SVC_Handler_21
def_service_handler SVC_Handler_22
def_service_handler SVC_Handler_23
def_service_handler SVC_Handler_24
def_service_handler SVC_Handler_25
def_service_handler SVC_Handler_26
def_service_handler SVC_Handler_27
def_service_handler SVC_Handler_28
def_service_handler SVC_Handler_29
def_service_handler SVC_Handler_30
def_service_handler SVC_Handler_31

declare_function SVC_Handler, .text
	/* Load the active stack into r1 */
	movs r0, #4
	mov r1, lr
	tst r0, r1               /* Test EXEC_RETURN for used stack */
	bne 0f
	mrs r1, msp              /* Move the main stack poiner to R1 */
	b 1f
0:
	mrs r1, psp              /* Move the process stack pointer to R1 */
1:

	/* Load the service number into r0 */
	ldr r0, [r1, #24]        /* First load the PC from the exception frame */
	subs r0, r0, #2          /* Back up to the SVC instruction */
	ldrb r0, [r0]            /* And load the second byte containing the SCV number */

	/* Load handler address in to r2 */
	lsls r3, r0, #2          /* Convert service number into table offset */
	ldr r2, =__svc_vector    /* Load the address of the service table */
	ldr r2, [r2, r3]         /* Load the service handler address */

	/* Invoke the service handler who will return from the service call */
	bx r2

	.pool
	.size SVC_Handler, . - SVC_Handler
