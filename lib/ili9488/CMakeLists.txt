add_library(ili9488 STATIC
    src/ili9488.c
    src/font5x7.c
)

target_include_directories(ili9488 PUBLIC include)
target_link_libraries(ili9488 PUBLIC pico_stdlib hardware_spi hardware_dma)
