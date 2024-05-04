#
# Copyright (C) 2024 Stephen Street
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

if (NOT TARGET pico_threads)

pico_add_library(pico_threads)

target_sources(pico_threads INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/threads.c
)

target_include_directories(pico_threads_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

target_link_libraries(pico_threads INTERFACE
	pico_scheduler
)

endif()
