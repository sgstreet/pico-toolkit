/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * errno.h
 *
 *  Created on: Mar 23, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#ifndef ERRNO_H_
#define ERRNO_H_

#ifndef __error_t_defined
typedef int error_t;
#define __error_t_defined 1
#endif

#include <sys/errno.h>

#define ERTOS (__ELASTERROR + 1)
#define ERESOURCE (ERTOS + 2)

extern error_t errno_from_rtos(int rtos);

#endif
