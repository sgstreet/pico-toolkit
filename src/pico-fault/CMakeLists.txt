#
# Copyright (C) 2024 Stephen Street
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

if (NOT TARGET pico_fault)

	pico_add_library(pico_fault)
	target_sources(pico_fault INTERFACE
		backtrace.c
		fault.c
		fault-m0plus-asm.S
	)
					
	target_include_directories(pico_fault_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
	
	target_compile_options(pico_fault INTERFACE -funwind-tables)
	target_compile_options(pico_fault INTERFACE -mpoke-function-name)
#	target_compile_options(pico_fault INTERFACE -fno-omit-frame-pointer)
	
	target_link_libraries(pico_fault INTERFACE
		toolkit_support
		pico_tls
	) 
	
endif()
