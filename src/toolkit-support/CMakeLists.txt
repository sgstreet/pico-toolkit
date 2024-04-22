if (NOT TARGET toolkit_support)

    pico_add_library(toolkit_support)

	target_include_directories(toolkit_support_headers INTERFACE include)
	
    target_sources(toolkit_support INTERFACE
    )
    
    target_link_libraries(toolkit_support INTERFACE
		pico_atomic
		cmsis_core
	)
    

endif()
