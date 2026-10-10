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

# Renderer + game test: panel contents must match a full composite after every frame.
add_executable(render_emu
    render_test.c
    ${REPO_ROOT}/src/render.c
    ${REPO_ROOT}/src/input.c
    ${REPO_ROOT}/src/physics.c
    ${REPO_ROOT}/src/tortoise.c
    ${REPO_ROOT}/src/turtle.c
    ${REPO_ROOT}/src/turtle_art.c
    ${REPO_ROOT}/src/meadow_image.c
    ${REPO_ROOT}/lib/ili9341/src/ili9341.c
    ${REPO_ROOT}/lib/ili9341/src/font5x7.c
)
target_include_directories(render_emu PRIVATE
    stubs
    .
    ${REPO_ROOT}/src
    ${REPO_ROOT}/lib/ili9341/include
)
target_compile_options(render_emu PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(render_emu PRIVATE EMU_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME render_and_game COMMAND render_emu)
