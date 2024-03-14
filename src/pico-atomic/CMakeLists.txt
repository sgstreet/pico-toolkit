if (NOT TARGET pico_atomic)

    pico_add_library(pico_atomic)

    target_sources(pico_atomic INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/pico-atomic.c
    )

    target_link_libraries(pico_atomic INTERFACE pico_sync)
    
endif()
