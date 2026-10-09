// Host-side test harness: implements the stubbed Pico SDK calls, emulates the
// ILI9341's SPI command protocol into a 320x240 framebuffer, and drives the
// firmware's main() through a scripted scenario via its sleep_ms() calls.
//
// The firmware never returns from main(), so the scenario ends by calling
// exit() from inside sleep_ms(). Screenshots are written as PPM files.

#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "hardware/rtc.h"
#include "hardware/spi.h"
#include "ili9341.h"

#ifndef EMU_OUTPUT_DIR
#define EMU_OUTPUT_DIR "."
#endif

#define W ILI9341_WIDTH
#define H ILI9341_HEIGHT

static int failures;
#define CHECK(cond, ...)                                     \
    do {                                                     \
        if (!(cond)) {                                       \
            failures++;                                      \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                    \
            fputc('\n', stderr);                             \
        }                                                    \
    } while (0)

// ---------------------------------------------------------------- panel model

static struct {
    bool dc, cs;
    int cmd;
    uint8_t args[4];
    int nargs;
    int x0, x1, y0, y1, px, py;
    int hi; // first byte of a pixel, -1 if none pending
    int madctl;
    bool awake, on;
    int writes_without_cs;
    int pixels_out_of_window;
    uint16_t fb[H][W];
} lcd = {.dc = true, .cs = true, .cmd = -1, .hi = -1, .madctl = -1};

static void lcd_byte(uint8_t b) {
    if (!lcd.dc) {
        lcd.cmd = b;
        lcd.nargs = 0;
        lcd.hi = -1;
        if (b == 0x2C) { lcd.px = lcd.x0; lcd.py = lcd.y0; }
        if (b == 0x11) lcd.awake = true;
        if (b == 0x29) lcd.on = true;
        return;
    }
    switch (lcd.cmd) {
    case 0x2A: // column address set
    case 0x2B: // page address set
        if (lcd.nargs < 4) lcd.args[lcd.nargs++] = b;
        if (lcd.nargs == 4) {
            int a = lcd.args[0] << 8 | lcd.args[1], c = lcd.args[2] << 8 | lcd.args[3];
            if (lcd.cmd == 0x2A) { lcd.x0 = a; lcd.x1 = c; }
            else { lcd.y0 = a; lcd.y1 = c; }
        }
        break;
    case 0x36:
        lcd.madctl = b;
        break;
    case 0x2C: // memory write
        if (lcd.hi < 0) { lcd.hi = b; break; }
        if (lcd.py > lcd.y1 || lcd.px >= W || lcd.py >= H) {
            lcd.pixels_out_of_window++;
        } else {
            lcd.fb[lcd.py][lcd.px] = (uint16_t)(lcd.hi << 8 | b);
        }
        lcd.hi = -1;
        if (++lcd.px > lcd.x1) { lcd.px = lcd.x0; lcd.py++; }
        break;
    default:
        break;
    }
}

static void save_ppm(const char *name) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", EMU_OUTPUT_DIR, name);
    FILE *f = fopen(path, "wb");
    if (!f) { CHECK(0, "cannot write %s", path); return; }
    fprintf(f, "P6 %d %d 255\n", W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint16_t c = lcd.fb[y][x];
            fputc((c >> 11) << 3, f);
            fputc(((c >> 5) & 0x3F) << 2, f);
            fputc((c & 0x1F) << 3, f);
        }
    }
    fclose(f);
}

static int count_pixels(int x, int y, int w, int h, uint16_t color) {
    int n = 0;
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            n += lcd.fb[j][i] == color;
    return n;
}

// ------------------------------------------------------------- SDK stubs

spi_inst_t *const spi0 = (spi_inst_t *)1;

void gpio_init(uint pin) { (void)pin; }
void gpio_set_dir(uint pin, bool out) { (void)pin; (void)out; }
void gpio_set_function(uint pin, int fn) { (void)pin; (void)fn; }

void gpio_put(uint pin, bool value) {
    if (pin == PIN_DC) lcd.dc = value;
    if (pin == PIN_CS) lcd.cs = value;
}

uint spi_init(spi_inst_t *spi, uint baudrate) {
    (void)spi; (void)baudrate;
    return 31250000; // 125 MHz / 4, what the RP2040 gives for 40 MHz
}

uint spi_set_baudrate(spi_inst_t *spi, uint baudrate) {
    (void)spi;
    return baudrate;
}

int spi_write_blocking(spi_inst_t *spi, const uint8_t *src, size_t len) {
    (void)spi;
    if (lcd.cs) lcd.writes_without_cs++;
    for (size_t i = 0; i < len; i++) lcd_byte(src[i]);
    return (int)len;
}

int spi_read_blocking(spi_inst_t *spi, uint8_t repeated_tx, uint8_t *dst, size_t len) {
    (void)spi; (void)repeated_tx;
    for (size_t i = 0; i < len; i++) dst[i] = 0x9C; // awake, display on
    return (int)len;
}

static datetime_t rtc_now;
void rtc_init(void) {}
bool rtc_set_datetime(const datetime_t *t) { rtc_now = *t; return true; }
bool rtc_get_datetime(datetime_t *t) { *t = rtc_now; return true; }

static const char *serial_input;
bool stdio_init_all(void) { return true; }

int getchar_timeout_us(uint32_t timeout_us) {
    (void)timeout_us;
    if (serial_input && *serial_input) return *serial_input++;
    return PICO_ERROR_TIMEOUT;
}

void sleep_us(uint64_t us) { (void)us; }

uint32_t time_us_32(void) { return 0; }

// ------------------------------------------------------------- scenario

static void check_colour_step(void) {
    static const struct {
        uint16_t color;
        const char *name;
    } steps[] = {
        {COLOR_RED, "colour_red"}, {COLOR_GREEN, "colour_green"},
        {COLOR_BLUE, "colour_blue"}, {COLOR_WHITE, "colour_white"},
    };
    static unsigned step;
    if (step >= sizeof steps / sizeof steps[0]) { CHECK(0, "unexpected 600 ms sleep"); return; }

    CHECK(lcd.fb[5][5] == steps[step].color, "%s: corner is 0x%04X", steps[step].name, lcd.fb[5][5]);
    CHECK(count_pixels(0, 100, W, 40, COLOR_BLACK) > 0, "%s: no label drawn", steps[step].name);
    save_ppm(steps[step].name);
    step++;
}

static bool rtc_is(int y, int mo, int d, int h, int mi, int s) {
    return rtc_now.year == y && rtc_now.month == mo && rtc_now.day == d &&
           rtc_now.hour == h && rtc_now.min == mi && rtc_now.sec == s;
}

static void loop_iteration(int frame) {
    switch (frame) {
    case 1:
        CHECK(lcd.madctl == ILI9341_MADCTL, "MADCTL 0x%02X", lcd.madctl);
        CHECK(lcd.awake && lcd.on, "panel not woken / switched on");
        CHECK(count_pixels(0, 70, W, 48, COLOR_WHITE) > 500, "time not drawn");
        CHECK(count_pixels(0, 140, W, 24, COLOR_CYAN) > 100, "date not drawn");
        CHECK(count_pixels(0, 175, W, 24, COLOR_ORANGE) > 100, "day not drawn");
        save_ppm("screen_clock");
        serial_input = "2026-10-11 09:05:59\r\n"; // a Sunday, shorter than most day names
        break;
    case 2:
        CHECK(rtc_is(2026, 10, 11, 9, 5, 59), "valid time not accepted");
        CHECK(rtc_now.dotw == 0, "day of week %d, expected 0 (Sunday)", rtc_now.dotw);
        serial_input = "2026-02-30 10:00:00\n"; // invalid date
        break;
    case 3:
        CHECK(rtc_is(2026, 10, 11, 9, 5, 59), "invalid time accepted");
        rtc_now.min = 6; // let the clock roll over to 09:06:00
        rtc_now.sec = 0;
        break;
    case 4:
        // "Sunday" is centred in a 9-character field with one leading blank cell,
        // so the first cell (x 79-96 at scale 3) must have been erased.
        CHECK(count_pixels(79, 175, 18, 24, COLOR_BLACK) == 18 * 24, "old day name not erased");
        save_ppm("screen_rollover");

        CHECK(lcd.writes_without_cs == 0, "%d SPI writes with CS high", lcd.writes_without_cs);
        CHECK(lcd.pixels_out_of_window == 0, "%d pixels outside window", lcd.pixels_out_of_window);
        printf("\n%s: %d failure(s), screenshots in %s\n", failures ? "FAILED" : "PASSED",
               failures, EMU_OUTPUT_DIR);
        exit(failures ? EXIT_FAILURE : EXIT_SUCCESS);
    }
}

void sleep_ms(uint32_t ms) {
    static int frame;
    if (ms == 600) check_colour_step(); // colour test step
    if (ms == 10) loop_iteration(++frame); // main loop tick
}
