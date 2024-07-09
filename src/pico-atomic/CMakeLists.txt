#
# Copyright (C) 2024 Stephen Street
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

if (NOT TARGET pico_atomic)

    pico_add_library(pico_atomic)

    target_sources(pico_atomic INTERFACE
	    ${CMAKE_CURRENT_LIST_DIR}/pico-atomic.c
    )

    target_link_libraries(pico_atomic INTERFACE pico_sync)
    
endif()
