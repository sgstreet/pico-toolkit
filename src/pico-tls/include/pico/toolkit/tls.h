/*
 * tls.h
 *
 *  Created on: Mar 7, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#ifndef _TLS_H_
#define _TLS_H_

#ifndef thread_local
#define	thread_local _Thread_local
#endif

#define core_local __section(".core_data")

#define cls_offset(datum) ({extern char __core_data[]; ((size_t)&datum - (size_t)&__core_data);})
#define cls_ptr() ({extern void *__aeabi_read_cls(void);__aeabi_read_cls();})
#define cls_core_ptr(core) ({extern void *__aeabi_read_core_cls(unsigned long);__aeabi_read_core_cls(core);})
#define cls_datum_ptr(datum) ((typeof(datum) *)(cls_ptr() + cls_offset(datum)))
#define cls_datum(datum) (*(cls_datum_ptr(datum)))
#define cls_datum_core_ptr(core, datum) ((typeof(datum) *)(cls_core_ptr(core) + cls_offset(datum)))
#define cls_datum_core(core, datum) (*(cls_datum_core_ptr(core, datum)))

extern void *__aeabi_read_core_cls(unsigned long core);
extern void *__aeabi_read_cls(void);
extern void *__aeabi_read_tp(void);

extern void _init_tls(void *__tls);
extern void _set_tls(void *tls);

#endif
