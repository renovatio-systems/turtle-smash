add_library(ili9341 STATIC
    src/ili9341.c
    src/font5x7.c
)

target_include_directories(ili9341 PUBLIC include)
target_link_libraries(ili9341 PUBLIC pico_stdlib hardware_spi)
