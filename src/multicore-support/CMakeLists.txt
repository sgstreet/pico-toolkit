#
# Copyright (C) 2024 Stephen Street
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

if (NOT TARGET multicore_support)

pico_add_library(multicore_support)

target_sources(multicore_support INTERFACE
	multicore-glue.c
	multicore-irq.c
)

pico_wrap_function(multicore_support irq_set_priority)
pico_wrap_function(multicore_support irq_get_priority)
pico_wrap_function(multicore_support irq_set_enabled)
pico_wrap_function(multicore_support irq_set_enabled)
pico_wrap_function(multicore_support irq_set_pending)

target_include_directories(multicore_support_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

target_link_libraries(multicore_support INTERFACE
	hardware_exception
	hardware_irq
	hardware_sync
	pico_bootrom
	pico_scheduler
	pico_multicore
	pico_nmi
)

endif()
