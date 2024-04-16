if (NOT TARGET pico_scheduler)

	pico_add_library(pico_scheduler)

	target_compile_definitions(pico_scheduler INTERFACE PICO_DEFAULT_IRQ_PRIORITY=0x40)
	target_compile_definitions(pico_scheduler INTERFACE SCHEDULER_REALTIME_IRQ_PRIORITY=0xff)
	target_compile_definitions(pico_scheduler INTERFACE SCHEDULER_MAX_IRQ_PRIORITY=0x00)

	target_sources(pico_scheduler INTERFACE
	    ${CMAKE_CURRENT_LIST_DIR}/scheduler-glue.c
	    ${CMAKE_CURRENT_LIST_DIR}/scheduler.c
	    ${CMAKE_CURRENT_LIST_DIR}/scheduler-m0plus-asm.S
	    ${CMAKE_CURRENT_LIST_DIR}/svc-asm.S
	)

	target_include_directories(pico_scheduler_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

	target_link_libraries(pico_scheduler INTERFACE
		hardware_exception
		hardware_uart
		hardware_clocks
		hardware_irq
		pico_bit_ops
		pico_divider
		pico_double
		pico_int64_ops
		pico_float
		pico_mem_ops
		cmsis_core
		picolibc_glue
		pico_tls
		pico_standard_link
	)

endif()
