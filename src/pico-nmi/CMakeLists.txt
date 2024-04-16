if (NOT TARGET pico_nmi)

pico_add_library(pico_nmi)

target_sources(pico_nmi INTERFACE
	nmi.c
)

target_include_directories(pico_nmi_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

target_link_libraries(pico_nmi INTERFACE
	hardware_exception
	hardware_irq
	toolkit_support
	pico_tls
	pico_atomic
)

endif()
