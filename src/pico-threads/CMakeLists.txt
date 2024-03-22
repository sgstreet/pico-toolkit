if (NOT TARGET pico_threads)

pico_add_library(pico_threads)

target_sources(pico_threads INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/threads.c
)

target_include_directories(pico_threads_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

target_link_libraries(pico_threads INTERFACE
	pico_rtos 
)

endif()
