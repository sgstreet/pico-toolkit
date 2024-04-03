/*
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

static core_local bool irq_enabled[32]  = { 0 };
static core_local uint8_t irq_priority[32]  = { 0 };

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
			irq_set_pending(irq);
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

	/* Clear any pending interrupt */
	irq_clear(irq);

	/* Then enable it */
	irq_set_enabled(irq, true);

	/* And update the cache */
	cls_datum(irq_enabled)[irq] = true;
}

static void disable_irq_cmd(IRQn_Type irq)
{
	/* Drop if system interrupt */
	if (irq < 0)
		return;

	/* Forward */
	irq_set_enabled(irq, false);

	/* And Update the cache */
	cls_datum(irq_enabled)[irq] = false;
}

static void set_priority_cmd(IRQn_Type irq, uint32_t priority)
{
	/* Set the priority */
	irq_set_priority(irq, priority);

	/* And Update the cache */
	cls_datum(irq_priority)[irq] = priority;
}

static void update_irq_config_cmd(IRQn_Type irq)
{
	cls_datum(irq_enabled)[irq] = irq_is_enabled(irq);
	cls_datum(irq_priority)[irq] = irq_get_priority(irq);
}

static __unused void multicore_irq_handler(void)
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
				uint32_t priority = (cmd >> 16) & 0x00ff;
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
__alias("multicore_irq_handler") void NMI_Handler(void);

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

__constructor void multicore_irq_init(void)
{
	/* More work depending on the core */
	if (get_core_num()) {

		/* Enable the core interrupt boosted to NMI */
//		set_priority_cmd(SIO_IRQ_PROC1_IRQn, 0);
//		enable_irq_cmd(SIO_IRQ_PROC1_IRQn);

		/* Boost the core interrupt to NMI, required by the scheduler */
		atomic_fetch_or(&syscfg_hw->proc1_nmi_mask, 1UL << SIO_IRQ_PROC1_IRQn);

		/* Hopefully we are going to left with the irq running */
		((void (*)(void))rom_func_lookup(rom_table_code('W', 'V')))();

	} else {

		/* Initialize core 1 */
		multicore_launch_core1(multicore_irq_init);

		/* Enable the core interrupt boosted to NMI */
//		set_priority_cmd(SIO_IRQ_PROC0_IRQn, 0);
//		enable_irq_cmd(SIO_IRQ_PROC0_IRQn);
		atomic_fetch_or(&syscfg_hw->proc0_nmi_mask, 1UL << SIO_IRQ_PROC0_IRQn);
	}
}
