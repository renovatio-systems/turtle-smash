add_executable(tft_clock main.c dragon.c tortoise.c tortoise_image.c joystick.c)

target_include_directories(tft_clock PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(tft_clock PRIVATE ili9341 pico_stdlib hardware_rtc)
target_compile_options(tft_clock PRIVATE -Wall)

pico_set_program_name(tft_clock "tft_clock")
pico_set_program_description(tft_clock "ILI9341 digital clock")
pico_set_program_version(tft_clock "${PROJECT_VERSION}")

# printf and time setting over USB CDC serial
pico_enable_stdio_usb(tft_clock 1)
pico_enable_stdio_uart(tft_clock 0)

# Produces tft_clock.uf2 for drag-and-drop flashing
pico_add_extra_outputs(tft_clock)
