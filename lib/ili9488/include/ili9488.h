#ifndef ILI9488_H
#define ILI9488_H

#include <stdbool.h>
#include <stdint.h>
#include "hardware/spi.h"

// Landscape orientation: 480 x 320.
#define ILI9488_WIDTH  480
#define ILI9488_HEIGHT 320

// Memory access control (0x36). 0x28 = MV | BGR (landscape, header on the left).
// If the image is mirrored or upside down try 0xE8 (MX | MY | MV | BGR).
// If red and blue are swapped, clear the BGR bit (0x08).
#ifndef ILI9488_MADCTL
#define ILI9488_MADCTL 0x28
#endif

#define ILI9488_RDDPM 0x0A // read display power mode, 0x9C = booster on, awake, normal, display on

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

// The panel takes 18-bit pixels over SPI: three bytes per pixel, R G B with the
// low two (green: low two of six) bits ignored. Packs one RGB565 colour that way.
#define ILI9488_BYTES_PER_PIXEL 3
static inline void ili9488_pack(uint16_t c, uint8_t *out) {
    out[0] = (c >> 8) & 0xF8;
    out[1] = (c >> 3) & 0xFC;
    out[2] = (c << 3) & 0xF8;
}

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
} ili9488_config_t;

// Initialises SPI, GPIOs and the panel. Returns the actual SPI clock in Hz.
uint ili9488_init(const ili9488_config_t *cfg);

void ili9488_fill_rect(int x, int y, int w, int h, uint16_t color);
void ili9488_fill_screen(uint16_t color);

// Copies a w*h block of RGB565 pixels to the screen at (x, y).
void ili9488_blit(int x, int y, int w, int h, const uint16_t *pixels);

// Sends a w*h block of already-packed pixels (ILI9488_BYTES_PER_PIXEL bytes each,
// see ili9488_pack). Returns immediately while DMA clocks the bytes out; `pixels`
// must stay untouched until the next ili9488_* call or ili9488_wait().
// Every other function in this file waits for a running transfer first.
void ili9488_blit_dma(int x, int y, int w, int h, const uint8_t *pixels);
void ili9488_wait(void);

// Character cell is 6*scale x 8*scale pixels (5x7 glyph plus 1px spacing),
// background included, so redrawing a character never flickers.
void ili9488_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale);
void ili9488_draw_string(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale);

// Reads one byte from a read command (e.g. ILI9488_RDDPM) over MISO.
uint8_t ili9488_read_reg8(uint8_t cmd);

#endif
