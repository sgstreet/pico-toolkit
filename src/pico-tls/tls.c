/*
 * tls.c
 *
 *  Created on: Apr 27, 2023
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <string.h>

#include <pico/toolkit/compiler.h>

#include <pico/toolkit/tls.h>

extern char __core_data[];
extern char __core_data_size[];
extern char __core_0[];
extern char __core_1[];

extern char __tls_block_offset[];

extern void *__arm32_tls_tcb_offset;
extern char __tdata_source[];
extern char __tdata_size[];
extern char __tbss_size[];
extern char __tbss_offset[];
extern char __tls_size;

core_local void *_tls = 0;

__naked void *__aeabi_read_cls(void)
{
	asm volatile (
		".syntax unified\n"
		"ldr r0, =0xd0000000\n"
		"ldr r0, [r0, #0]\n"
		"cmp r0, #0\n"
		"beq 0f\n"
		"ldr r0, =__core_1\n"
		"mov pc, lr\n"
		"0: \n"
		"ldr r0, =__core_0\n"
		"mov pc, lr\n"
	);}

__naked void *__aeabi_read_core_cls(unsigned long core)
{
	asm volatile (
		".syntax unified\n"
		"cmp r0, #0\n"
		"beq 0f\n"
		"ldr r0, =__core_1\n"
		"mov pc, lr\n"
		"0: \n"
		"ldr r0, =__core_0\n"
		"mov pc, lr\n"
	);
}

__naked void *__aeabi_read_tp(void)
{
	asm volatile (
		".syntax unified\n"
		"push {r1, lr}\n"
		"ldr r0,=__core_data\n"
		"ldr r1,=_tls\n"
		"subs r1, r1, r0 /* calculate offset of _tls using the core_data block */\n"
		"bl __aeabi_read_cls\n"
		"ldr r0, [r0, r1]\n"
		"pop {r1, pc}\n"
	);
}

void _set_tls(void *tls)
{
	cls_datum(_tls) = tls - ((size_t)&__arm32_tls_tcb_offset);
}

void _init_tls(void *tls)
{
	/* Copy tls initialized data */
	memcpy(tls, __tdata_source, (size_t)__tdata_size);

	/* Clear tls zero data */
	memset(tls +(size_t)__tbss_offset, 0, (size_t)__tbss_size);
}

void _cls_tls_init(void)
{
	/* Load the core local blocks */
	memcpy(__aeabi_read_cls(), __core_data, (size_t)__core_data_size);

	/* Initialize both cores initial TLS blocks */
	_init_tls(__aeabi_read_cls() + (size_t)__tls_block_offset);

	/* Initialize the core local tls blocks */
	cls_datum(_tls) = __aeabi_read_cls() + (size_t)__tls_block_offset - ((size_t)&__arm32_tls_tcb_offset);
}
__attribute__((used, section(".preinit_array.00040"))) void *preinit_cls_tls_init = _cls_tls_init;
