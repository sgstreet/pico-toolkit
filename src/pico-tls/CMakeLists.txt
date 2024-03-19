if (NOT TARGET pico_tls)
	pico_add_library(pico_tls)
	
	target_sources(pico_tls INTERFACE
		${CMAKE_CURRENT_LIST_DIR}/tls.c
	)
	
	target_include_directories(pico_tls_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
	target_link_libraries(pico_tls INTERFACE picolibc_glue)
	
endif()
