/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * cmsis-rtos2-validation.c
 *
 *  Created on: Mar 22, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <stdio.h>
#include <stdlib.h>

#include <pico/toolkit/cmsis.h>

#include <hardware/irq.h>
#include <hardware/gpio.h>
#include <hardware/uart.h>

#include "cmsis_rv2.h"

#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define NUM_WORKERS 7

extern void Interrupt26_Handler(void);
extern void Interrupt27_Handler(void);

int picolibc_putc(char c, FILE *file);
int picolibc_getc(FILE *file);
void *_rtos2_alloc(size_t size);
void _rtos2_release(void *ptr);

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

void *_rtos2_alloc(size_t size)
{
	return calloc(1, size);
}

void _rtos2_release(void *ptr)
{
	free(ptr);
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

int stdout_putchar(char txchar)
{
	putchar(txchar);
}

int main (void)
{
	irq_set_exclusive_handler(26, Interrupt26_Handler);
	irq_set_exclusive_handler(27, Interrupt27_Handler);
	irq_set_enabled(26, true);
	irq_set_enabled(27, true);

	cmsis_rv2();
}



