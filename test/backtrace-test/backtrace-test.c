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


static void dump_backtrace(const backtrace_t *backtrace, int count)
{
	for (int i = 0; i < count; ++i)
		printf("%p - %s@%p\n", backtrace[i].address, backtrace[i].name, backtrace[i].function);
}

static void ball_location(void *function, int ball)
{
	printf("%s - %d\n", backtrace_function_name((uint32_t)function), ball);
}

static int pong(int ball, backtrace_t *backtrace, int size)
{
	ball_location(pong, ball);

	if (ball > 0)
		return ping(ball - 1, backtrace, size);
	else
		return backtrace_unwind(backtrace, size);
}

static int ping(int ball, backtrace_t *backtrace, int size)
{
	ball_location(pong, ball);

	if (ball > 0)
		return pong(ball - 1, backtrace, size);
	else
		return backtrace_unwind(backtrace, size);
}

int main(int argc, char **argv)
{
	backtrace_t backtrace[BACKTRACE_SIZE];
	
	/* Play ball */
	int count = ping(10, backtrace, BACKTRACE_SIZE);

	/* Dump the backtrace */
	dump_backtrace(backtrace, count);

	/* All good */
	return 0;
}

