#ifndef _CMSIS_H_
#define _CMSIS_H_

#include <RP2040.h>

#include "compiler.h"

static __always_inline inline uint32_t disable_interrupts(void)
{
	uint32_t primask = __get_PRIMASK();
	__set_PRIMASK(1);
	return primask;
}

static __always_inline inline void enable_interrupts(uint32_t primask)
{
	__set_PRIMASK(primask);
}

#endif
