/*
 * multicore-irq.h
 *
 *  Created on: Mar 26, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#ifndef _MULTICORE_IRQ_H_
#define _MULTICORE_IRQ_H_

void multicore_irq_init(void);

void multicore_irq_set_enable(uint num, uint core, bool enabled);
bool multicore_irq_is_enabled(uint num, uint core);

void multicore_irq_set_priority(uint num, uint core, uint8_t hardware_priority);
uint mulitcore_irq_get_priority(uint num, uint core);

void multicore_irq_set_pending(uint num, uint core);
void multicore_irq_clear(uint num, uint core);

#endif
