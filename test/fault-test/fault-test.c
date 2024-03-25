/*
 * Copyright 2015 Stephen Street <stephen@redrocketcomputing.com>
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. 
 */

#include <stdio.h>
#include <stdint.h>

#include <pico/toolkit/compiler.h>
#include <pico/toolkit/fault.h>
#include <pico/toolkit/backtrace.h>

#include <hardware/gpio.h>
#include <hardware/uart.h>

#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define BACKTRACE_SIZE 25

int picolibc_putc(char c, FILE *file);
int picolibc_getc(FILE *file);

static int ping(int ball, backtrace_t *backtrace, int size);
static int pong(int ball, backtrace_t *backtrace, int size);

int picolibc_putc(char c, FILE *file)
{
	if (c == '\n')
		uart_putc(UART_ID, '\r');

	uart_putc(UART_ID, c);

	return c;
}

int picolibc_getc(FILE *file)
{
	return uart_getc(UART_ID);
}

__constructor void console_init(void)
{
	/* Set up our UART with the required speed. */
	uart_init(UART_ID, BAUD_RATE);

	/*
	 * Set the TX and RX pins by using the function select on the GPIO
	 * Set datasheet for more information on function select
	 */
	gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
	gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
}

void save_fault(const struct cortexm_fault *fault)
{
	struct backtrace fault_backtrace[10];
	backtrace_frame_t backtrace_frame;

	/* Setup for a backtrace */
	backtrace_frame.fp = fault->r7;
	backtrace_frame.lr = fault->LR;
	backtrace_frame.sp = fault->SP;

	/* I'm not convinced this is correct,  */
	backtrace_frame.pc = fault->exception_return == 0xfffffff1 ? fault->LR : fault->PC;

	/* Try the unwind */
	int backtrace_entries = _backtrace_unwind(fault_backtrace, array_sizeof(fault_backtrace), &backtrace_frame);

	/* Print header */
	printf("\ncore %u faulted at 0x%08x with PSR 0x%08x\n", fault->core, fault->PC, fault->PSR);

	/* Dump the registers first */
	printf("\tr0:  0x%08x r1:  0x%08x r2:  0x%08x r3:  0x%08x\n", fault->r0, fault->r1, fault->r2, fault->r3);
	printf("\tr4:  0x%08x r5:  0x%08x r6:  0x%08x r7:  0x%08x\n", fault->r4, fault->r5, fault->r6, fault->r7);
	printf("\tr8:  0x%08x r9:  0x%08x r10: 0x%08x r11: 0x%08x\n", fault->r8, fault->r9, fault->r10, fault->r11);
	printf("\tIP:  0x%08x LR:  0x%08x SP:  0x%08x PC:  0x%08x\n", fault->IP, fault->LR, fault->SP, fault->PC);

	/* Followed by the back trace */
	printf("\nbacktrace:\n");
	for (size_t i = 0; i < backtrace_entries; ++i)
		printf("\t%s@%p - %p\n", fault_backtrace[i].name, fault_backtrace[i].function, fault_backtrace[i].address);
}

int main(int argc, char **argv)
{
	volatile uint32_t *addr = (uint32_t *)0x5fffffff;

	__asm volatile (
		"ldr r7, =0xdeadbeef \n"
		"mov r3, r7 \n"
		"mov r10, r7 \n"
		"mov r12, r7 \n"
	);

	return *addr;
}

