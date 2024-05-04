/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * nmi-test.c
 *
 *  Created on: Mar 13, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <stdio.h>
#include <stdint.h>

#include <RP2040.h>

#include <hardware/gpio.h>
#include <hardware/uart.h>
#include <hardware/timer.h>
#include <hardware/irq.h>

#include <pico/toolkit/iob.h>
#include <pico/toolkit/nmi.h>

#define UART_ID uart0
#define BAUD_RATE 115200

#define UART_TX_PIN 0
#define UART_RX_PIN 1

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

	puts("console initialized");
}

static void blink_led(void)
{
	gpio_xor_mask(1UL << PICO_DEFAULT_LED_PIN);
}

static void irq_26(void)
{
}

static void irq_27(void)
{
}

static void irq_28(void)
{
}

static void irq_29(void)
{
}

static void irq_30(void)
{
}

int main(int argc, char **argv)
{
	printf("Initializing LED\n");
	gpio_init(PICO_DEFAULT_LED_PIN);
	gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

	nmi_set_enable(24, true);
	nmi_set_enable(26, true);
	nmi_set_enable(27, true);
	nmi_set_enable(28, true);
	nmi_set_enable(29, true);
	nmi_set_enable(30, true);

	nmi_set_enable(30, false);
	nmi_set_enable(28, false);
	nmi_set_enable(29, false);
	nmi_set_enable(26, false);
	nmi_set_enable(27, false);

	/* The NMI mask is diverted before the NVIC and so will never trigger */
	while (true) {
		irq_set_pending(24);
		busy_wait_ms(125);
	}

	return 0;
}
