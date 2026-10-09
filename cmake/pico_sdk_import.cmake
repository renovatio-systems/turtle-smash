# Locates the Raspberry Pi Pico SDK (simplified version of the SDK's
# external/pico_sdk_import.cmake).
#
#   - Set PICO_SDK_PATH (env or -D) to an existing pico-sdk checkout, or
#   - set PICO_SDK_FETCH_FROM_GIT=1 to download it automatically.

if (DEFINED ENV{PICO_SDK_PATH} AND (NOT PICO_SDK_PATH))
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif ()

if (DEFINED ENV{PICO_SDK_FETCH_FROM_GIT} AND (NOT PICO_SDK_FETCH_FROM_GIT))
    set(PICO_SDK_FETCH_FROM_GIT $ENV{PICO_SDK_FETCH_FROM_GIT})
endif ()

set(PICO_SDK_FETCH_FROM_GIT_TAG "2.3.1" CACHE STRING "pico-sdk tag to download")

if (NOT PICO_SDK_PATH)
    if (PICO_SDK_FETCH_FROM_GIT)
        include(FetchContent)
        FetchContent_Declare(
            pico_sdk
            GIT_REPOSITORY https://github.com/raspberrypi/pico-sdk
            GIT_TAG ${PICO_SDK_FETCH_FROM_GIT_TAG}
        )
        FetchContent_GetProperties(pico_sdk)
        if (NOT pico_sdk_POPULATED)
            message("Downloading Raspberry Pi Pico SDK ${PICO_SDK_FETCH_FROM_GIT_TAG}")
            FetchContent_Populate(pico_sdk)
        endif ()
        set(PICO_SDK_PATH ${pico_sdk_SOURCE_DIR})
    else ()
        message(FATAL_ERROR
            "Pico SDK not found. Set PICO_SDK_PATH or pass -DPICO_SDK_FETCH_FROM_GIT=1")
    endif ()
endif ()

get_filename_component(PICO_SDK_PATH "${PICO_SDK_PATH}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
if (NOT EXISTS ${PICO_SDK_PATH}/pico_sdk_init.cmake)
    message(FATAL_ERROR "'${PICO_SDK_PATH}' does not appear to contain the Pico SDK")
endif ()

set(PICO_SDK_PATH ${PICO_SDK_PATH} CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
