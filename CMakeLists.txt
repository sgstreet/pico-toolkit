#
# Copyright (C) 2024 Stephen Street
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

cmake_minimum_required(VERSION 3.13)

if (NOT TARGET _pico_toolkit_inclusion_marker)
    add_library(_pico_toolkit_inclusion_marker INTERFACE)

    # Pull in PICO SDK (must be before project)
    include(pico_sdk_import.cmake)
    if (PICO_SDK_VERSION_STRING VERSION_LESS "1.2.0")
        message(FATAL_ERROR "Require at least Raspberry Pi Pico SDK version 1.2.0")
    endif()

    project(pico_toolkit C CXX)

    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 17)

    pico_is_top_level_project(PICO_TOOLKIT_TOP_LEVEL_PROJECT)

    if (NOT PICO_TOOLKIT_PATH)
        set(PICO_TOOLKIT_PATH ${CMAKE_CURRENT_LIST_DIR})
    endif()
    set(PICO_TOOLKIT_PATH "${PICO_TOOLKIT_PATH}" CACHE PATH "Path to Pico Toolkit")

    # The real work gets done in post_init which is called at the end of pico_sdk_init
    list(APPEND PICO_SDK_POST_LIST_FILES ${CMAKE_CURRENT_LIST_DIR}/post_init.cmake)
    if (PICO_TOOLKIT_TOP_LEVEL_PROJECT)
        message("pico_toolkit: initialize SDK since we're the top-level")
        # Initialize the SDK
        pico_sdk_init()
    else()
        set(PICO_SDK_POST_LIST_FILES ${PICO_SDK_POST_LIST_FILES} PARENT_SCOPE)
    endif()
endif()