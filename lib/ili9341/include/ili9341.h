#ifndef ILI9341_H
#define ILI9341_H

#include <stdbool.h>
#include <stdint.h>
#include "hardware/spi.h"

// Landscape orientation: 320 x 240.
#define ILI9341_WIDTH  320
#define ILI9341_HEIGHT 240

// Memory access control (0x36). 0x28 = MV | BGR (landscape, header on the left).
// If the image is mirrored or upside down try 0xE8 (MX | MY | MV | BGR).
// If red and blue are swapped, clear the BGR bit (0x08).
#ifndef ILI9341_MADCTL
#define ILI9341_MADCTL 0x28
#endif

#define ILI9341_RDDPM 0x0A // read display power mode, 0x9C = booster on, awake, normal, display on

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define COLOR_BLACK  0x0000
#define COLOR_WHITE  0xFFFF
#define COLOR_RED    0xF800
#define COLOR_GREEN  0x07E0
#define COLOR_BLUE   0x001F
#define COLOR_CYAN   0x07FF
#define COLOR_YELLOW 0xFFE0
#define COLOR_ORANGE RGB565(255, 140, 0)
#define COLOR_GREY   RGB565(120, 120, 120)

typedef struct {
    spi_inst_t *spi;
    uint pin_sck;
    uint pin_mosi;
    uint pin_miso;
    uint pin_cs;
    uint pin_dc;
    uint pin_rst;
    int pin_bl;    // backlight GPIO, or -1 if LED is tied to 3V3
    uint baud;     // requested SPI clock in Hz
} ili9341_config_t;

// Initialises SPI, GPIOs and the panel. Returns the actual SPI clock in Hz.
uint ili9341_init(const ili9341_config_t *cfg);

void ili9341_fill_rect(int x, int y, int w, int h, uint16_t color);
void ili9341_fill_screen(uint16_t color);

// Character cell is 6*scale x 8*scale pixels (5x7 glyph plus 1px spacing),
// background included, so redrawing a character never flickers.
void ili9341_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale);
void ili9341_draw_string(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale);

// Reads one byte from a read command (e.g. ILI9341_RDDPM) over MISO.
uint8_t ili9341_read_reg8(uint8_t cmd);

#endif
