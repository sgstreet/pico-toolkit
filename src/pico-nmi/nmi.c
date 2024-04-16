/*
 * pico-nmi.c
 *
 *  Created on: Apr 10, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <assert.h>
#include <stdatomic.h>

#include <hardware/structs/syscfg.h>

#include <hardware/irq.h>
#include <pico/platform.h>

#include <pico/toolkit/tls.h>
#include <pico/toolkit/cmsis.h>
#include <pico/toolkit/nmi.h>
#include <pico/toolkit/spinlock.h>

#if 0
#undef core_local
#undef cls_datum
#define core_local
#define cls_datum
#endif

#define nmi_proc_mask (*(io_rw_32 *)(&syscfg_hw->proc0_nmi_mask + get_core_num()))

static core_local irq_handler_t active_handlers[NUM_IRQS + 1] = { 0 };

void NMI_Handler(void)
{
	/* Dispatch all boosted nmi handlers */
	irq_handler_t *pos = cls_datum(active_handlers);
	irq_handler_t current;
	while ((current = *pos++) != 0)
		current();
}

void nmi_set_enable(uint num, bool enabled)
{
	assert(num < NUM_IRQS);

	/* Skip if we are already in the correct state */
	if (nmi_is_enabled(num) == enabled)
		return;

	/* Project the active handlers table */
	uint32_t state = disable_interrupts();
	uint32_t nmi_state = nmi_proc_mask;
	nmi_proc_mask = 0;

	if (enabled) {

		/* Look for the end of the table place the handler there */
		for (size_t idx = 0; idx < NUM_IRQS; ++idx)
			if (cls_datum(active_handlers)[idx] == 0) {

				/* Add the handler to the table */
				cls_datum(active_handlers)[idx] = irq_get_vtable_handler(num);

				/* And to the hardware mask */
				nmi_state |= (1UL << num);
				break;
			}

	} else {

		/* Remove and compress table in one pass */
		for (size_t idx = 0, found = false; idx < NUM_IRQS && cls_datum(active_handlers)[idx] != 0; ++idx)
			if (found || (found = cls_datum(active_handlers)[idx] == irq_get_vtable_handler(num)))
				cls_datum(active_handlers)[idx] = cls_datum(active_handlers)[idx + 1];

		/* Remove interrupt from the IRQ mask */
		nmi_state &= ~(1UL << num);
	}

	/* Let it fly */
	nmi_proc_mask = nmi_state;
	enable_interrupts(state);
}

bool nmi_is_enabled(uint num)
{
	return (nmi_proc_mask & (1UL << num)) != 0;
}

uint64_t nmi_mask(void)
{
	return atomic_exchange(&syscfg_hw->proc0_nmi_mask, 0);
}

void nmi_unmask(uint64_t state)
{
	atomic_exchange(&syscfg_hw->proc0_nmi_mask, state);
}
