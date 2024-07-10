/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * rtt-test.c
 *
 *  Created on: April 20, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <stdio.h>
#include <stdint.h>

#include <RP2040.h>

#include <hardware/gpio.h>
#include <hardware/uart.h>

#include <pico/toolkit/iob.h>
#include <pico/unique_id.h>
#include <pico/toolkit/rtt/SEGGER_RTT.h>

#define UART_ID uart0
#define BAUD_RATE 115200

#define UART_TX_PIN 0
#define UART_RX_PIN 1

int picolibc_putc(char c, FILE *file);
int picolibc_getc(FILE *file);
int stddiag_putc(char c, FILE *file);

struct iob _diag = IOB_DEV_SETUP(stddiag_putc, 0, 0, 0, __SWR, 0);
FILE *const diag = (FILE *)&_diag;

int picolibc_putc(char c, FILE *file)
{
	uart_putc(UART_ID, c);
	return c;
}

int picolibc_getc(FILE *file)
{
	return uart_getc(UART_ID);
}

static __attribute__((constructor)) void console_init(void)
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

static __attribute__((constructor)) void diag_init(void)
{
	SEGGER_RTT_Init();
}

int stddiag_putc(char c, FILE *file)
{
	if (c == '\n')
		SEGGER_RTT_PutChar(0, '\r');
	SEGGER_RTT_PutChar(0, c);
	return c;
}

static uint64_t board_id(void)
{
	pico_unique_board_id_t board_id;
	pico_get_unique_board_id(&board_id);
	return *(uint64_t *)(board_id.id);
}

int main()
{
	/* Send out a character without any conversions */
	putc('A', stdout);
	fputc('A', diag);

	/* Send out a character but do CR/LF conversions */
	putc('B', stdout);
	fputc('B', diag);

	/* Send out a string, with CR/LF conversions */
	puts(" Hello, PICOLIBC!");
	fputs(" Hello, PICOLIBC!\n", diag);

	/* Print also */
	uint32_t clock = SystemCoreClock;
	uint64_t id = board_id();
	printf("Hello from board 0x%llx running at %luHz\n", id, clock);
	fprintf(diag, "Hello from board 0x%llx running at %luHz\n", id, clock);
}
