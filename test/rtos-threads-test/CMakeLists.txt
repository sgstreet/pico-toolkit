#
# Copyright (C) 2024 Stephen Street
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

add_executable(rtos-threads-test rtos-threads-test.c)

pico_set_linker_script(rtos-threads-test ${PICO_TOOLKIT_PATH}/src/picolibc-glue/pico-toolkit-flash.ld)

target_link_libraries(rtos-threads-test
	hardware_gpio
	hardware_uart
	cmsis_core
	pico_atomic
	pico_tls
	picolibc_glue
	pico_fault
	pico_scheduler
	pico_threads
	pico_runtime
)

pico_add_extra_outputs(rtos-threads-test)