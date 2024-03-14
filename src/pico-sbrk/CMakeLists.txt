if (NOT TARGET pico_sbrk)

    pico_add_library(pico_sbrk)

    target_sources(pico_sbrk INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/sbrk.c
    )

    target_link_libraries(pico_sbrk INTERFACE pico_atomic)
    
endif()
