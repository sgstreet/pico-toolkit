if (NOT TARGET pico_rtos)

pico_add_library(pico_rtos)

target_sources(pico_rtos INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/rtos-glue.c
    ${CMAKE_CURRENT_LIST_DIR}/scheduler.c
    ${CMAKE_CURRENT_LIST_DIR}/scheduler-m0plus-asm.S
    ${CMAKE_CURRENT_LIST_DIR}/svc-asm.S
)

target_include_directories(pico_rtos_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

target_link_libraries(pico_rtos INTERFACE 
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
