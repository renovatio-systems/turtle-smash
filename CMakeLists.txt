cmake_minimum_required(VERSION 3.13)

# Board definition for the "RP2040 USB" stick (4 MB flash). Override with
# -DPICO_BOARD=pico to build for a Raspberry Pi Pico.
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD rp2040_usb_stick CACHE STRING "Pico SDK board")

# Must come before project()
include(cmake/pico_sdk_import.cmake)

project(rp2040_ili9488_clock VERSION 1.0.0 LANGUAGES C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(lib/ili9488)
add_subdirectory(src)
