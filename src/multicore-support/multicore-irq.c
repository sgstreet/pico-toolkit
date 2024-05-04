/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * multicore-irq.c
 *
 *  Created on: Mar 26, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <stdatomic.h>
#include <stdint.h>

#include <hardware/irq.h>
#include <hardware/structs/syscfg.h>

#include <pico/toolkit/tls.h>
#include <pico/toolkit/cmsis.h>
#include <pico/toolkit/nmi.h>
#include <pico/toolkit/multicore-irq.h>

#include <pico/platform.h>
#include <pico/multicore.h>
#include <pico/bootrom.h>

#define MULTICORE_COMMAND_MSK 0xf0000000

enum multicore_cmd
{
	MULTICORE_EXECUTE_FLASH = 0x10000000,
	MULTICORE_EXECUTE_SRAM = 0x20000000,
	MULTICORE_EVENT = 0x80000000,
	MULTICORE_PEND_IRQ = 0x90000000,
	MULTICORE_CLEAR_IRQ = 0xa0000000,
	MULTICORE_IRQ_ENABLE = 0xb0000000,
	MULTICORE_IRQ_DISABLE = 0xc0000000,
	MULTICORE_SET_PRIORITY = 0xd0000000,
	MULTICORE_UPDATE_CONFIG = 0xe0000000,
};

extern void __real_irq_set_priority(uint num, uint8_t hardware_priority);
extern uint __real_irq_get_priority(uint num);
extern void __real_irq_set_enabled(uint num, bool enabled);
extern bool __real_irq_is_enabled(uint num);
extern void __real_irq_set_pending(uint num);

void __wrap_irq_set_priority(uint num, uint8_t hardware_priority);
uint __wrap_irq_get_priority(uint num);
void __wrap_irq_set_enabled(uint num, bool enabled);
bool __wrap_irq_is_enabled(uint num);
void __wrap_irq_set_pending(uint num);

static core_local bool irq_enabled[NUM_IRQS]  = { 0 };
static core_local uint8_t irq_priority[NUM_IRQS]  = { 0 };
static uint8_t irq_affinity[NUM_IRQS] = { 0 };

static void pend_irq_cmd(IRQn_Type irq)
{
	/* These interrupts are in the interrupt control and status register */
	switch (irq) {

		case NonMaskableInt_IRQn:
			SCB->ICSR = SCB_ICSR_NMIPENDSET_Msk;
			break;

		case PendSV_IRQn:
			SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
			break;

		case SysTick_IRQn:
			SCB->ICSR = SCB_ICSR_PENDSTSET_Msk;
			break;

		default:
			__real_irq_set_pending(irq);
			break;
	}

	/* Ensure the memory write is complete */
	__DMB();
}

static void clear_irq_cmd(IRQn_Type irq)
{
	/* NVIC interrupt */
	irq_clear(irq);
	__DMB();
}

static void enable_irq_cmd(IRQn_Type irq)
{
	/* Drop if system interrupt */
	if (irq < 0)
		return;

	/* Enable, passing real time (-1) priorities to the NMI */
	if (cls_datum(irq_priority)[irq] != UINT8_MAX) {

		/* Clear any pending interrupt */
		irq_clear(irq);

		/* Then enable it */
		__real_irq_set_enabled(irq, true);
	} else
		nmi_set_enable(irq, true);

	/* And update the cache */
	cls_datum(irq_enabled)[irq] = true;
}

static void disable_irq_cmd(IRQn_Type irq)
{
	/* Drop if system interrupt */
	if (irq < 0)
		return;

	/* Disables passing real time (-1) priorites to the NMI */
	if (cls_datum(irq_priority)[irq] != UINT8_MAX)
		__real_irq_set_enabled(irq, false);
	else
		nmi_set_enable(irq, false);

	/* And Update the cache */
	cls_datum(irq_enabled)[irq] = false;
}

static void set_priority_cmd(IRQn_Type irq, uint8_t priority)
{
	/* Use 0xff (-1) as the real time interrupt priority i.e. boost to NMI, Use cmsis to correctly handle system interrupts */
	if (priority != UINT8_MAX)
		NVIC_SetPriority(irq, priority);

	/* And Update the cache */
	cls_datum(irq_priority)[irq] = priority;
}

static void update_irq_config_cmd(IRQn_Type irq)
{
	cls_datum(irq_enabled)[irq] = irq_is_enabled(irq);
	cls_datum(irq_priority)[irq] = irq_get_priority(irq);
}

static void multicore_irq_handler(void)
{
	uint32_t cmd;

	/* Empty the fifo */
	while (multicore_fifo_rvalid()) {

		/* There will always be data */
		cmd = multicore_fifo_pop_blocking();

		/* Handle the command */
		switch (cmd & MULTICORE_COMMAND_MSK) {

			case MULTICORE_EXECUTE_FLASH:
			case MULTICORE_EXECUTE_SRAM: {
				((void (*)(void))cmd)();
				break;
			}

			case MULTICORE_EVENT:
				break;

			case MULTICORE_PEND_IRQ: {
				IRQn_Type irq = (cmd & 0xffff) - 16;
				pend_irq_cmd(irq);
				break;
			}

			case MULTICORE_CLEAR_IRQ: {
				IRQn_Type irq = (cmd & 0xffff) - 16;
				clear_irq_cmd(irq);
				break;
			}

			case MULTICORE_IRQ_ENABLE: {
				IRQn_Type irq = (cmd & 0xffff) - 16;
				enable_irq_cmd(irq);
				break;
			}

			case MULTICORE_IRQ_DISABLE: {
				IRQn_Type irq = (cmd & 0xffff) - 16;
				disable_irq_cmd(irq);
				break;
			}

			case MULTICORE_SET_PRIORITY: {
				IRQn_Type irq = (cmd & 0xffff) - 16;
				uint8_t priority = (cmd >> 16) & 0x00ff;
				set_priority_cmd(irq, priority);
				break;
			}

			case MULTICORE_UPDATE_CONFIG: {
				IRQn_Type irq = (cmd & 0xffff) - 16;
				update_irq_config_cmd(irq);
				break;
			}

			default:
				break;
		}
	}

	/* Clear the fifo state */
	multicore_fifo_clear_irq();
}
__alias("multicore_irq_handler") void SIO_IRQ_PROC0_Handler(void);
__alias("multicore_irq_handler") void SIO_IRQ_PROC1_Handler(void);

void multicore_irq_set_enable(uint num, uint core, bool enabled)
{
	/* Are we running on the target core? */
	if (core == get_core_num()) {
		irq_set_enabled(num, enabled);
		return;
	}

	/* Nope, forward to the other core */
	if (enabled)
		multicore_fifo_push_blocking(MULTICORE_IRQ_ENABLE | num + 16);
	else
		multicore_fifo_push_blocking(MULTICORE_IRQ_DISABLE | num + 16);
}

bool multicore_irq_is_enabled(uint num, uint core)
{
	/* TODO FIX ME, this will not work is users call irq_set_enabled or NVIC_EnableIRQ directly */
	return cls_datum_core(core, irq_enabled)[num];
}

void multicore_irq_set_priority(uint num, uint core, uint8_t hardware_priority)
{
	if (core == get_core_num()) {
		irq_set_priority(num,  hardware_priority);
		return;
	}

	multicore_fifo_push_blocking(MULTICORE_SET_PRIORITY | hardware_priority << 16 | num + 16);
}

uint mulitcore_irq_get_priority(uint num, uint core)
{
	/* TODO FIX ME, this will not work is users call irq_set_priority or NVIC_SetPriority directly */
	return cls_datum_core(core, irq_priority)[num];
}

void multicore_irq_set_pending(uint num, uint core)
{
	/* Are we running on the target core? */
	if (core == get_core_num()) {
		pend_irq_cmd(num);
		return;
	}

	/* Nope, forward to the other core */
	multicore_fifo_push_blocking(MULTICORE_PEND_IRQ | num + 16);
}

void multicore_irq_clear(uint num, uint core)
{
	/* Are we running on the target core? */
	if (core == get_core_num()) {
		clear_irq_cmd(num);
		return;
	}

	/* Nope, forward to the other core */
	multicore_fifo_push_blocking(MULTICORE_CLEAR_IRQ | num + 16);
}

void irq_set_affinity(uint num, uint core)
{
	assert(num < NUM_IRQS);
	irq_affinity[num] = core;
}

uint irq_get_affinity(uint num)
{
	assert(num < NUM_IRQS);
	return irq_affinity[num];
}

void __wrap_irq_set_priority(uint num, uint8_t hardware_priority)
{
	assert(num < NUM_IRQS);

	if (irq_affinity[num] == get_core_num()) {
		set_priority_cmd(num, hardware_priority);
		return;
	}

	multicore_fifo_push_blocking(MULTICORE_SET_PRIORITY | hardware_priority << 16 | num + 16);
}

uint __wrap_irq_get_priority(uint num)
{
	assert(num < NUM_IRQS);

	return cls_datum_core(irq_affinity[num], irq_priority)[num];
}

void __wrap_irq_set_enabled(uint num, bool enabled)
{
	assert(num < NUM_IRQS);

	/* Are we running on the target core? */
	if (irq_affinity[num] == get_core_num()) {

		/* Bypass the multicore fifo */
		if (enabled)
			enable_irq_cmd(num);
		else
			disable_irq_cmd(num);

		return;
	}

	/* Nope, forward to the other core */
	if (enabled)
		multicore_fifo_push_blocking(MULTICORE_IRQ_ENABLE | num + 16);
	else
		multicore_fifo_push_blocking(MULTICORE_IRQ_DISABLE | num + 16);

}

bool __wrap_irq_is_enabled(uint num)
{
	assert(num < NUM_IRQS);

	return cls_datum_core(irq_affinity[num], irq_enabled)[num];
}

void __wrap_irq_set_pending(uint num)
{
	assert(num < NUM_IRQS);

	/* Are we running on the target core? */
	if (irq_affinity[num] == get_core_num()) {
		pend_irq_cmd(num);
		return;
	}

	/* Nope, forward to the other core */
	multicore_fifo_push_blocking(MULTICORE_PEND_IRQ | num + 16);
}

__constructor void multicore_irq_init(void)
{
	/* More work depending on the core */
	if (get_core_num()) {

		/* Mark as real time and enable our end of the fifo */
		irq_set_affinity(SIO_IRQ_PROC1, get_core_num());
		irq_set_priority(SIO_IRQ_PROC1, UINT8_MAX);
		irq_set_enabled(SIO_IRQ_PROC1, true);

		/* Hopefully we are going to left with the irq running */
		((void (*)(void))rom_func_lookup(rom_table_code('W', 'V')))();

	} else {

		/* Initialize core 1 */
		multicore_launch_core1(multicore_irq_init);

		/* Boost the core interrupt to NMI */
		irq_set_affinity(SIO_IRQ_PROC0, get_core_num());
		irq_set_priority(SIO_IRQ_PROC0, UINT8_MAX);
		irq_set_enabled(SIO_IRQ_PROC0, true);
	}
}
