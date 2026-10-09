# Host build: runs the firmware against an emulated ILI9341 (no hardware or
# ARM toolchain needed).
#
#   cmake -S tests/host -B build-host && cmake --build build-host && ctest --test-dir build-host -V
cmake_minimum_required(VERSION 3.13)
project(tft_clock_host_test C)

set(CMAKE_C_STANDARD 11)
set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

add_executable(tft_clock_emu
    ${REPO_ROOT}/src/main.c
    ${REPO_ROOT}/lib/ili9341/src/ili9341.c
    ${REPO_ROOT}/lib/ili9341/src/font5x7.c
    emulator.c
)

target_include_directories(tft_clock_emu PRIVATE
    stubs
    ${REPO_ROOT}/src
    ${REPO_ROOT}/lib/ili9341/include
)
target_compile_options(tft_clock_emu PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(tft_clock_emu PRIVATE SHOW_COLOUR_BARS=0 SHOW_DRAGON=0 SHOW_TORTOISE=0 EMU_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")

enable_testing()
add_test(NAME firmware_emulation COMMAND tft_clock_emu)
