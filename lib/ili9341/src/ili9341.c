#include "ili9341.h"

#include "font5x7.h"
#include "pico/stdlib.h"

#define READ_BAUD (1000 * 1000) // reads are only specified up to ~6 MHz

static ili9341_config_t cfg;
static uint8_t line_buf[ILI9341_WIDTH * 2];

// Command, data length, data...  Terminated by a zero command.
// Power/gamma values are the widely used Adafruit/TFT_eSPI defaults.
static const uint8_t init_cmds[] = {
    0xEF, 3, 0x03, 0x80, 0x02,
    0xCF, 3, 0x00, 0xC1, 0x30,             // power control B
    0xED, 4, 0x64, 0x03, 0x12, 0x81,       // power on sequence
    0xE8, 3, 0x85, 0x00, 0x78,             // driver timing A
    0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02, // power control A
    0xF7, 1, 0x20,                         // pump ratio
    0xEA, 2, 0x00, 0x00,                   // driver timing B
    0xC0, 1, 0x23,                         // power control 1
    0xC1, 1, 0x10,                         // power control 2
    0xC5, 2, 0x3E, 0x28,                   // VCOM control 1
    0xC7, 1, 0x86,                         // VCOM control 2
    0x36, 1, ILI9341_MADCTL,               // memory access control
    0x37, 1, 0x00,                         // vertical scroll start
    0x3A, 1, 0x55,                         // 16 bits per pixel
    0xB1, 2, 0x00, 0x18,                   // frame rate 79 Hz
    0xB6, 3, 0x08, 0x82, 0x27,             // display function control
    0xF2, 1, 0x00,                         // 3-gamma off
    0x26, 1, 0x01,                         // gamma curve 1
    0xE0, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
              0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00, // positive gamma
    0xE1, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
              0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F, // negative gamma
    0x00,
};

static inline void cs(bool level) { gpio_put(cfg.pin_cs, level); }
static inline void dc(bool level) { gpio_put(cfg.pin_dc, level); }

static void write_cmd(uint8_t c) {
    dc(0);
    cs(0);
    spi_write_blocking(cfg.spi, &c, 1);
    cs(1);
}

static void write_data(const uint8_t *d, size_t n) {
    dc(1);
    cs(0);
    spi_write_blocking(cfg.spi, d, n);
    cs(1);
}

static void set_window(int x0, int y0, int x1, int y1) {
    uint8_t d[4] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
    write_cmd(0x2A); // column address set
    write_data(d, 4);
    d[0] = y0 >> 8; d[1] = y0 & 0xFF; d[2] = y1 >> 8; d[3] = y1 & 0xFF;
    write_cmd(0x2B); // page address set
    write_data(d, 4);
    write_cmd(0x2C); // memory write
}

static void gpio_out(uint pin, bool level) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, level);
}

uint ili9341_init(const ili9341_config_t *c) {
    cfg = *c;

    uint actual = spi_init(cfg.spi, cfg.baud); // mode 0, 8 bit, MSB first
    gpio_set_function(cfg.pin_sck, GPIO_FUNC_SPI);
    gpio_set_function(cfg.pin_mosi, GPIO_FUNC_SPI);
    gpio_set_function(cfg.pin_miso, GPIO_FUNC_SPI);

    gpio_out(cfg.pin_cs, 1);
    gpio_out(cfg.pin_dc, 1);
    gpio_out(cfg.pin_rst, 1);
    if (cfg.pin_bl >= 0) gpio_out(cfg.pin_bl, 1);

    // Hardware reset, then software reset.
    sleep_ms(5);
    gpio_put(cfg.pin_rst, 0);
    sleep_ms(20);
    gpio_put(cfg.pin_rst, 1);
    sleep_ms(150);
    write_cmd(0x01);
    sleep_ms(150);

    for (const uint8_t *p = init_cmds; *p; p += 2 + p[1]) {
        write_cmd(p[0]);
        write_data(p + 2, p[1]);
    }

    write_cmd(0x11); // sleep out
    sleep_ms(120);
    write_cmd(0x29); // display on
    sleep_ms(20);

    return actual;
}

void ili9341_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ILI9341_WIDTH) w = ILI9341_WIDTH - x;
    if (y + h > ILI9341_HEIGHT) h = ILI9341_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    for (int i = 0; i < w; i++) {
        line_buf[2 * i] = color >> 8;
        line_buf[2 * i + 1] = color & 0xFF;
    }
    set_window(x, y, x + w - 1, y + h - 1);
    dc(1);
    cs(0);
    for (int row = 0; row < h; row++)
        spi_write_blocking(cfg.spi, line_buf, 2 * w);
    cs(1);
}

void ili9341_fill_screen(uint16_t color) {
    ili9341_fill_rect(0, 0, ILI9341_WIDTH, ILI9341_HEIGHT, color);
}

void ili9341_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale) {
    int w = 6 * scale, h = 8 * scale;
    if (scale < 1 || x < 0 || y < 0 || x + w > ILI9341_WIDTH || y + h > ILI9341_HEIGHT)
        return;

    const uint8_t *glyph = font5x7_glyph(c);
    set_window(x, y, x + w - 1, y + h - 1);
    dc(1);
    cs(0);
    for (int row = 0; row < h; row++) {
        int gy = row / scale;
        for (int col = 0; col < w; col++) {
            int gx = col / scale;
            bool on = gx < 5 && gy < 7 && (glyph[gx] >> gy) & 1;
            uint16_t px = on ? fg : bg;
            line_buf[2 * col] = px >> 8;
            line_buf[2 * col + 1] = px & 0xFF;
        }
        spi_write_blocking(cfg.spi, line_buf, 2 * w);
    }
    cs(1);
}

void ili9341_draw_string(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale) {
    for (; *s; s++, x += 6 * scale)
        ili9341_draw_char(x, y, *s, fg, bg, scale);
}

uint8_t ili9341_read_reg8(uint8_t cmd) {
    // Undocumented index register (0xD9) used by Adafruit's driver to make
    // single-byte reads work on the 4-wire SPI interface.
    uint8_t index = 0x10;
    write_cmd(0xD9);
    write_data(&index, 1);

    spi_set_baudrate(cfg.spi, READ_BAUD);
    uint8_t value = 0;
    dc(0);
    cs(0);
    spi_write_blocking(cfg.spi, &cmd, 1);
    dc(1);
    spi_read_blocking(cfg.spi, 0x00, &value, 1);
    cs(1);
    spi_set_baudrate(cfg.spi, cfg.baud);
    return value;
}
