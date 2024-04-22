if (NOT TARGET pico_rtt)

	pico_add_library(pico_rtt)
	
	target_sources(pico_rtt INTERFACE
		SEGGER_RTT.c		
		pico-rtt.c
	)
	
	target_compile_definitions(pico_rtt INTERFACE PICO_TOOLKIT_RTT_LOCK)
	target_include_directories(pico_rtt_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
	
	target_link_libraries(pico_rtt INTERFACE
		toolkit_support
		picolibc_glue
	)

endif()
