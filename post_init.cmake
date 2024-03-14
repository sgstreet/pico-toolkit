add_library(pico_toolkit_included INTERFACE)
target_compile_definitions(pico_toolkit_included INTERFACE
        -DPICO_TOOLKIT=1
        )

pico_add_platform_library(pico_toolkit_included)

# note as we're a .cmake included by the SDK, we're relative to the pico-sdk build
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/src ${CMAKE_BINARY_DIR}/pico_toolkit/src)

if (PICO_TOOLKIT_TESTS_ENABLED OR PICO_TOOLKIT_TOP_LEVEL_PROJECT)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/test ${CMAKE_BINARY_DIR}/pico_toolkit/test)
endif ()

