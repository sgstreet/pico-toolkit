#
# Copyright (C) 2024 Stephen Street
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

if (NOT TARGET picolibc_glue)

    pico_add_library(picolibc_glue)
    
	add_dependencies(picolibc_glue picolibc)
	
    target_sources(picolibc_glue INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/iob.c
            ${CMAKE_CURRENT_LIST_DIR}/retarget-lock.c
            ${CMAKE_CURRENT_LIST_DIR}/sbrk.c
    )

	target_compile_definitions(picolibc_glue INTERFACE PICO_USE_MALLOC_MUTEX=0) 
	target_compile_definitions(picolibc_glue INTERFACE PICO_MALLOC_PANIC=0) 
	
	target_link_libraries(picolibc_glue INTERFACE 
		toolkit_support
	)
	
	target_include_directories(picolibc_glue_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
	target_compile_options(picolibc_glue INTERFACE --specs=$ENV{PICOLIBC_PATH}/lib/picolibc.specs)
	target_compile_options(picolibc_glue INTERFACE -O0)
	target_compile_options(picolibc_glue INTERFACE -g)
	target_link_options(picolibc_glue INTERFACE --specs=$ENV{PICOLIBC_PATH}/lib/picolibc.specs)
	target_link_options(picolibc_glue INTERFACE -T${CMAKE_CURRENT_LIST_DIR}/picolibc-empty.ld)
	
endif()
