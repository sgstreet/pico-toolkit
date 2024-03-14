# This is a copy of <PICO_TOOLKIT_PATH>/external/pico_toolkit_import.cmake

# This can be dropped into an external project to help locate pico-toolkit
# It should be include()ed prior to project()

if (DEFINED ENV{PICO_TOOLKIT_PATH} AND (NOT PICO_TOOLKIT_PATH))
    set(PICO_TOOLKIT_PATH $ENV{PICO_TOOLKIT_PATH})
    message("Using PICO_TOOLKIT_PATH from environment ('${PICO_TOOLKIT_PATH}')")
endif ()

if (DEFINED ENV{PICO_TOOLKIT_FETCH_FROM_GIT} AND (NOT PICO_TOOLKIT_FETCH_FROM_GIT))
    set(PICO_TOOLKIT_FETCH_FROM_GIT $ENV{PICO_TOOLKIT_FETCH_FROM_GIT})
    message("Using PICO_TOOLKIT_FETCH_FROM_GIT from environment ('${PICO_TOOLKIT_FETCH_FROM_GIT}')")
endif ()

if (DEFINED ENV{PICO_TOOLKIT_FETCH_FROM_GIT_PATH} AND (NOT PICO_TOOLKIT_FETCH_FROM_GIT_PATH))
    set(PICO_TOOLKIT_FETCH_FROM_GIT_PATH $ENV{PICO_TOOLKIT_FETCH_FROM_GIT_PATH})
    message("Using PICO_TOOLKIT_FETCH_FROM_GIT_PATH from environment ('${PICO_TOOLKIT_FETCH_FROM_GIT_PATH}')")
endif ()

if (NOT PICO_TOOLKIT_PATH)
    if (PICO_TOOLKIT_FETCH_FROM_GIT)
        include(FetchContent)
        set(FETCHCONTENT_BASE_DIR_SAVE ${FETCHCONTENT_BASE_DIR})
        if (PICO_TOOLKIT_FETCH_FROM_GIT_PATH)
            get_filename_component(FETCHCONTENT_BASE_DIR "${PICO_TOOLKIT_FETCH_FROM_GIT_PATH}" REALPATH BASE_DIR "${CMAKE_SOURCE_DIR}")
        endif ()
        FetchContent_Declare(
                pico_toolkit
                GIT_REPOSITORY https://github.com/sgstreet/pico-toolkit
                GIT_TAG master
        )
        if (NOT pico_toolkit)
            message("Downloading Raspberry Pi Pico Extras")
            FetchContent_Populate(pico_toolkit)
            set(PICO_TOOLKIT_PATH ${pico_toolkit_SOURCE_DIR})
        endif ()
        set(FETCHCONTENT_BASE_DIR ${FETCHCONTENT_BASE_DIR_SAVE})
    else ()
        if (PICO_SDK_PATH AND EXISTS "${PICO_SDK_PATH}/../pico-toolkit")
            set(PICO_TOOLKIT_PATH ${PICO_SDK_PATH}/../pico-toolkit)
            message("Defaulting PICO_TOOLKIT_PATH as sibling of PICO_SDK_PATH: ${PICO_TOOLKIT_PATH}")
        else()
            message(FATAL_ERROR
                    "PICO TOOLKIT location was not specified. Please set PICO_TOOLKIT_PATH or set PICO_TOOLKIT_FETCH_FROM_GIT to on to fetch from git."
                    )
        endif()
    endif ()
endif ()

set(PICO_TOOLKIT_PATH "${PICO_TOOLKIT_PATH}" CACHE PATH "Path to the PICO TOOLKIT")
set(PICO_TOOLKIT_FETCH_FROM_GIT "${PICO_TOOLKIT_FETCH_FROM_GIT}" CACHE BOOL "Set to ON to fetch copy of PICO TOOLKIT from git if not otherwise locatable")
set(PICO_TOOLKIT_FETCH_FROM_GIT_PATH "${PICO_TOOLKIT_FETCH_FROM_GIT_PATH}" CACHE FILEPATH "location to download TOOLKIT")

get_filename_component(PICO_TOOLKIT_PATH "${PICO_TOOLKIT_PATH}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
if (NOT EXISTS ${PICO_TOOLKIT_PATH})
    message(FATAL_ERROR "Directory '${PICO_TOOLKIT_PATH}' not found")
endif ()

set(PICO_TOOLKIT_PATH ${PICO_TOOLKIT_PATH} CACHE PATH "Path to the PICO TOOLKIT" FORCE)

add_subdirectory(${PICO_TOOLKIT_PATH} pico_toolkit)
