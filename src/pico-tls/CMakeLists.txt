#
# Copyright (C) 2024 Stephen Street
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

if (NOT TARGET pico_tls)
	pico_add_library(pico_tls)
	
	target_sources(pico_tls INTERFACE
		${CMAKE_CURRENT_LIST_DIR}/tls.c
	)
	
	target_include_directories(pico_tls_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
	target_link_libraries(pico_tls INTERFACE picolibc_glue)
	
endif()
