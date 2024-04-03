if (NOT TARGET multicore_support)

pico_add_library(multicore_support)

target_sources(multicore_support INTERFACE
	multicore-glue.c
	multicore-irq.c
)

target_include_directories(multicore_support_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

target_link_libraries(multicore_support INTERFACE
	hardware_exception
	hardware_irq
	hardware_sync
	pico_bootrom
	pico_multicore
	pico_scheduler
)

endif()
