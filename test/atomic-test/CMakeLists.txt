#
# Copyright (C) 2024 Stephen Street
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

add_executable(atomic-test atomic-test.c)

pico_set_linker_script(atomic-test ${PICO_TOOLKIT_PATH}/src/picolibc-glue/pico-toolkit-flash.ld)

target_link_libraries(atomic-test
	hardware_gpio
	hardware_uart
	cmsis_core
	picolibc_glue
	toolkit_support
	pico_atomic
	pico_runtime
)

pico_add_extra_outputs(atomic-test)