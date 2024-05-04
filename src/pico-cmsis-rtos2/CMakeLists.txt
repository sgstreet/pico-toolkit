#
# Copyright (C) 2024 Stephen Street
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

if (NOT TARGET pico_cmsis_rtos2)

pico_add_library(pico_cmsis_rtos2)

target_sources(pico_cmsis_rtos2 INTERFACE
	cmsis-rtos2-deque.c
	cmsis-rtos2-eventflags.c
	cmsis-rtos2-generic-wait.c
	cmsis-rtos2-kernel.c
	cmsis-rtos2-message-queue.c
	cmsis-rtos2-mutex.c
	cmsis-rtos2-pool.c
	cmsis-rtos2-semaphore.c
	cmsis-rtos2-thread.c
	cmsis-rtos2-timer.c
)
target_include_directories(pico_cmsis_rtos2_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

target_link_libraries(pico_cmsis_rtos2 INTERFACE
	pico_scheduler
)

endif()
