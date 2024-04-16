/*
 * nmi.h
 *
 *  Created on: Apr 10, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#ifndef _NMI_H_
#define _NMI_H_

#include <stdint.h>
#include <stdbool.h>

void nmi_set_enable(uint num, bool enabled);
bool nmi_is_enabled(uint num);

uint64_t nmi_mask(void);
void nmi_unmask(uint64_t state);

#endif
