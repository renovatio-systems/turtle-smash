#include "ili9488.h"

#include "font5x7.h"
#include "hardware/dma.h"
#include "pico/stdlib.h"

#define READ_BAUD (1000 * 1000) // reads are only specified up to a few MHz

static ili9488_config_t cfg;
static uint8_t line_buf[ILI9488_WIDTH * ILI9488_BYTES_PER_PIXEL];
static int dma_chan = -1;
static bool dma_busy; // a DMA pixel transfer is in flight, CS still low

// Command, data length, data...  Terminated by a zero command.
// Register values are the ones common to ILI9488 3.5" SPI modules.
static const uint8_t init_cmds[] = {
    0xE0, 15, 0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78, 0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A,
              0x0F,                         // positive gamma
    0xE1, 15, 0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45, 0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37,
              0x0F,                         // negative gamma
    0xC0, 2, 0x17, 0x15,                    // power control 1
    0xC1, 1, 0x41,                          // power control 2
    0xC5, 3, 0x00, 0x12, 0x80,              // VCOM control
    0x36, 1, ILI9488_MADCTL,                // memory access control
    0x3A, 1, 0x66,                          // 18 bits per pixel: the only format over 4-wire SPI
    0xB0, 1, 0x00,                          // interface mode control
    0xB1, 1, 0xA0,                          // frame rate 60 Hz
    0xB4, 1, 0x02,                          // display inversion: 2-dot
    0xB6, 3, 0x02, 0x02, 0x3B,              // display function control
    0xE9, 1, 0x00,                          // set image function: off
    0xF7, 4, 0xA9, 0x51, 0x2C, 0x82,        // adjust control 3
    0x00,
};

static inline void cs(bool level) { gpio_put(cfg.pin_cs, level); }
static inline void dc(bool level) { gpio_put(cfg.pin_dc, level); }

static void write_cmd(uint8_t c) {
    ili9488_wait(); // commands must not interleave with a running pixel transfer
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

uint ili9488_init(const ili9488_config_t *c) {
    cfg = *c;
    dma_chan = dma_claim_unused_channel(true);

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

// Packs `n` copies of an RGB565 colour into line_buf, ready to send as a row.
static void fill_line(uint16_t color, int n) {
    uint8_t px[ILI9488_BYTES_PER_PIXEL];
    ili9488_pack(color, px);
    for (int i = 0; i < n; i++)
        for (int b = 0; b < ILI9488_BYTES_PER_PIXEL; b++)
            line_buf[ILI9488_BYTES_PER_PIXEL * i + b] = px[b];
}

void ili9488_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ILI9488_WIDTH) w = ILI9488_WIDTH - x;
    if (y + h > ILI9488_HEIGHT) h = ILI9488_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    fill_line(color, w);
    set_window(x, y, x + w - 1, y + h - 1);
    dc(1);
    cs(0);
    for (int row = 0; row < h; row++)
        spi_write_blocking(cfg.spi, line_buf, ILI9488_BYTES_PER_PIXEL * w);
    cs(1);
}

void ili9488_blit(int x, int y, int w, int h, const uint16_t *pixels) {
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > ILI9488_WIDTH || y + h > ILI9488_HEIGHT) return;
    set_window(x, y, x + w - 1, y + h - 1);
    dc(1);
    cs(0);
    for (int row = 0; row < h; row++, pixels += w) {
        for (int i = 0; i < w; i++) ili9488_pack(pixels[i], &line_buf[ILI9488_BYTES_PER_PIXEL * i]);
        spi_write_blocking(cfg.spi, line_buf, ILI9488_BYTES_PER_PIXEL * w);
    }
    cs(1);
}

void ili9488_blit_dma(int x, int y, int w, int h, const uint8_t *pixels) {
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > ILI9488_WIDTH || y + h > ILI9488_HEIGHT) return;
    set_window(x, y, x + w - 1, y + h - 1); // also waits for the previous transfer
    dc(1);
    cs(0);
    dma_channel_config dc_cfg = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dc_cfg, DMA_SIZE_8);
    channel_config_set_dreq(&dc_cfg, spi_get_dreq(cfg.spi, true));
    dma_channel_configure(dma_chan, &dc_cfg, &spi_get_hw(cfg.spi)->dr, pixels,
                          (uint)(ILI9488_BYTES_PER_PIXEL * w * h), true);
    dma_busy = true;
}

void ili9488_wait(void) {
    if (!dma_busy) return;
    dma_busy = false;
    dma_channel_wait_for_finish_blocking(dma_chan);
    while (spi_is_busy(cfg.spi)) tight_loop_contents();
    cs(1);
    // Transmit-only DMA still fills the receive FIFO; empty it so later reads are clean.
    while (spi_is_readable(cfg.spi)) (void)spi_get_hw(cfg.spi)->dr;
    spi_get_hw(cfg.spi)->icr = SPI_SSPICR_RORIC_BITS;
}

void ili9488_fill_screen(uint16_t color) {
    ili9488_fill_rect(0, 0, ILI9488_WIDTH, ILI9488_HEIGHT, color);
}

void ili9488_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale) {
    int w = 6 * scale, h = 8 * scale;
    if (scale < 1 || x < 0 || y < 0 || x + w > ILI9488_WIDTH || y + h > ILI9488_HEIGHT)
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
            ili9488_pack(on ? fg : bg, &line_buf[ILI9488_BYTES_PER_PIXEL * col]);
        }
        spi_write_blocking(cfg.spi, line_buf, ILI9488_BYTES_PER_PIXEL * w);
    }
    cs(1);
}

void ili9488_draw_string(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale) {
    for (; *s; s++, x += 6 * scale)
        ili9488_draw_char(x, y, *s, fg, bg, scale);
}

uint8_t ili9488_read_reg8(uint8_t cmd) {
    // The 4-wire read clocks one dummy bit after the command, so the value comes
    // out shifted by one bit across two bytes.
    write_cmd(cmd);
    spi_set_baudrate(cfg.spi, READ_BAUD);
    uint8_t raw[2] = {0, 0};
    dc(1);
    cs(0);
    spi_read_blocking(cfg.spi, 0x00, raw, 2);
    cs(1);
    spi_set_baudrate(cfg.spi, cfg.baud);
    return (uint8_t)(raw[0] << 1 | raw[1] >> 7);
}
