#
# Copyright (C) 2024 Stephen Street
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

add_executable(rtos-benchmark)

target_sources(rtos-benchmark PRIVATE
	bench_all.c
	bench_interrupt_latency_test.c
	bench_malloc_free_test.c
	bench_message_queue_test.c
	bench_mutex_lock_unlock_test.c
	bench_sem_context_switch_test.c
	bench_sem_signal_release_test.c
	bench_thread_switch_yield_test.c
	bench_thread_test.c
	bench_utils.c
	bench_porting_layer_cmsis_rtos2.c
)

pico_set_linker_script(rtos-benchmark ${PICO_TOOLKIT_PATH}/src/picolibc-glue/pico-toolkit-flash.ld)

target_link_libraries(rtos-benchmark
	hardware_gpio
	hardware_uart
	hardware_timer
	pico_fault
	pico_cmsis_rtos2
	pico_runtime
)

pico_add_extra_outputs(rtos-benchmark)