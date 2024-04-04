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

