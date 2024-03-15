if (NOT TARGET picolibc_glue)

    pico_add_library(picolibc_glue)

    target_sources(picolibc_glue INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/iob.c
    )

	target_include_directories(picolibc_glue_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
	target_compile_options(picolibc_glue INTERFACE --specs=$ENV{PICOLIBC_PATH}/lib/picolibc.specs)
	target_link_options(picolibc_glue INTERFACE --specs=$ENV{PICOLIBC_PATH}/lib/picolibc.specs)
	target_link_options(picolibc_glue INTERFACE -T${CMAKE_CURRENT_LIST_DIR}/picolibc-empty.ld)
	
endif()
