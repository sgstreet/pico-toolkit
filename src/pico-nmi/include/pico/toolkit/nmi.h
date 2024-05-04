/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
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
