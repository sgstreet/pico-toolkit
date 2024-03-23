if (NOT TARGET picolibc_glue)

    pico_add_library(picolibc_glue)

    target_sources(picolibc_glue INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/iob.c
            ${CMAKE_CURRENT_LIST_DIR}/retarget-lock.c
            ${CMAKE_CURRENT_LIST_DIR}/disable-unwinder.c
    )

	target_compile_definitions(picolibc_glue INTERFACE PICO_USE_MALLOC_MUTEX=0) 
	
	target_link_libraries(picolibc_glue INTERFACE toolkit_support pico_sbrk)
	
	target_include_directories(picolibc_glue_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
	target_compile_options(picolibc_glue INTERFACE --specs=$ENV{PICOLIBC_PATH}/lib/picolibc.specs)
	target_link_options(picolibc_glue INTERFACE --specs=$ENV{PICOLIBC_PATH}/lib/picolibc.specs)
	target_link_options(picolibc_glue INTERFACE -T${CMAKE_CURRENT_LIST_DIR}/picolibc-empty.ld)
	
endif()
