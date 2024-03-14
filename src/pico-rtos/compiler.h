#ifndef COMPILER_H_
#define COMPILER_H_

#include <stdbool.h>
#include <sys/cdefs.h>
#include <stddef.h>

#ifdef __cplusplus
#define __extern extern "C"
#else
#define __extern extern
#endif

#ifndef __alias
#define __alias(name) __attribute__((alias(name)))
#endif

#ifndef __weak_alias
#define __weak_alias(name) __attribute__((weak, alias(name)))
#endif

#ifndef __extern_inline
#define __extern_inline extern inline __attribute__((gnu_inline))
#endif

#ifndef __unused
#define __unused __attribute__((unused))
#endif

#ifndef __always_inline
#define __always_inline __attribute__((always_inline))
#endif

#ifndef __section
#define __section(name) __attribute__((section(name)))
#endif

#ifndef __isr_section
#define __isr_section __attribute__((section(".isr")))
#endif

#ifndef __fast_section
//#define __fast_section __attribute__((section(".fast")))
#define __fast_section
#endif

#ifndef __optimize
#define __optimize __attribute__((optimize("-O3")))
#endif

#ifndef __optimize_size
#define __optimize_size __attribute__((optimize("-Os")))
#endif

#ifndef __optimize_fast
#define __optimize_fast __attribute__((optimize("-Ofast")))
#endif

#ifndef __no_optimize
#define __no_optimize __attribute__((optimize("-O0")))
#endif

#ifndef __weak
#define __weak __attribute__((weak))
#endif

#ifndef __noreturn
#define __noreturn __attribute__((noreturn))
#endif

#ifndef __naked
#define __naked __attribute__((naked))
#endif

#ifndef __aligned
#define __aligned(x) __attribute__((aligned(x)))
#endif

#ifndef __constructor
#define __constructor __attribute__((constructor))
#endif

#ifndef __destructor
#define __destructor __attribute__((destructor))
#endif

#ifndef __constructor_priority
#define __constructor_priority(priority) __attribute__((constructor(priority)))
#endif

#ifndef __destructor_priority
#define __destructor_priority(priority) __attribute__((destructor(priority)))
#endif

#ifndef __packed
#define __packed __attribute__((packed))
#endif

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define array_sizeof(array) (sizeof(array) / sizeof((array)[0]) + sizeof(typeof(int[1 - 2 * !!__builtin_types_compatible_p(typeof(array), typeof(&array[0]))])) * 0)

#define barrier() do { asm volatile ("" : : : "memory"); } while (0)

#define PREINIT_PRIORITY(PRIORITY) ".preinit_array.00" #PRIORITY
#define PREINIT_SECTION(SECTION) ".preinit_array." #SECTION
#define PREINIT(NAME) void *__attribute__((used, section(PREINIT_SECTION(NAME)))) preinit_##NAME = NAME
#define PREINIT_WITH_PRIORITY(NAME, PRIORITY) void *__attribute__((used, section(PREINIT_PRIORITY(PRIORITY)))) preinit_##NAME = NAME

#endif
