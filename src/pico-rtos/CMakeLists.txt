if (NOT TARGET pico_rtos)

pico_add_library(pico_rtos)

target_sources(pico_rtos INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/rtos-toolkit-glue.c
    ${CMAKE_CURRENT_LIST_DIR}/scheduler.c
    ${CMAKE_CURRENT_LIST_DIR}/scheduler-m0plus-asm.S
    ${CMAKE_CURRENT_LIST_DIR}/svc-asm.S
)

target_include_directories(pico_rtos_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(pico_rtos INTERFACE hardware_exception cmsis_core picolibc_glue pico_atomic pico_tls pico_runtime)

endif()
