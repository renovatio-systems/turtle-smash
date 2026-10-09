// RP2040 + 2.2" ILI9341 SPI TFT: colour test, then a digital clock.
//
// The RP2040 RTC has no battery backup, so on power-up the clock starts from
// the firmware build time. Set the real time over USB serial by sending:
//     YYYY-MM-DD HH:MM:SS     e.g. 2026-10-07 14:30:00
// Send "?" to print the current time and the display's status register.

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "hardware/rtc.h"
#include "dragon.h"
#include "ili9341.h"
#include "input.h"
#include "render.h"
#include "pico/stdlib.h"
#include "tortoise.h"

// Layout (320 x 240 landscape). A character cell is 6*scale x 8*scale.
#define TIME_SCALE 6
#define TIME_Y     70
#define DATE_SCALE 3
#define DATE_Y     140
#define DAY_SCALE  3
#define DAY_Y      175
#define DAY_FIELD  9 // strlen("Wednesday")

static const char *const DAY_NAMES[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
};
static const char *const MONTH_NAMES[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

// Characters currently on screen, so only changed digits are redrawn.
static char shown_time[16], shown_date[16], shown_day[16];

static uint spi_baud; // actual SPI clock, for the status report

static int day_of_week(int y, int m, int d) { // 0 = Sunday
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static int days_in_month(int y, int m) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return (m == 2 && leap) ? 29 : days[m - 1];
}

static bool set_clock(int y, int mo, int d, int h, int mi, int s) {
    if (y < 2000 || y > 4095 || mo < 1 || mo > 12 || d < 1 || d > days_in_month(y, mo) ||
        h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59)
        return false;

    datetime_t t = {
        .year = y, .month = mo, .day = d, .dotw = day_of_week(y, mo, d),
        .hour = h, .min = mi, .sec = s,
    };
    if (!rtc_set_datetime(&t)) return false;
    sleep_us(64); // new value takes ~3 RTC clock cycles to appear
    return true;
}

static void set_clock_from_build_time(void) {
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = "";
    int y = 2026, mo = 1, d = 1, h = 0, mi = 0, s = 0;

    sscanf(__DATE__, "%3s %d %d", mon, &d, &y); // "Oct  7 2026"
    sscanf(__TIME__, "%d:%d:%d", &h, &mi, &s);  // "14:30:00"
    const char *p = strstr(months, mon);
    if (p) mo = (int)(p - months) / 3 + 1;
    set_clock(y, mo, d, h, mi, s);
}

static void print_time(void) {
    datetime_t t;
    rtc_get_datetime(&t);
    printf("%04d-%02d-%02d %02d:%02d:%02d %s\n", t.year, t.month, t.day,
           t.hour, t.min, t.sec, DAY_NAMES[t.dotw]);
}

static bool handle_line(const char *line) {
    int y, mo, d, h, mi, s;
    if (sscanf(line, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6) {
        if (set_clock(y, mo, d, h, mi, s)) {
            printf("Clock set: ");
            print_time();
            return true;
        }
        printf("Invalid date/time\n");
    } else if (line[0] == '?') {
        print_time();
        printf("Display: SPI %u Hz, RDDPM 0x%02X (0x9C = on, 0x00/0xFF = no reply)\n",
               spi_baud, ili9341_read_reg8(ILI9341_RDDPM));
#if SHOW_TORTOISE
        const render_stats_t *rs = render_stats();
        printf("Render: last flush %u rects, %u px, %u us (worst %u us)\n", (unsigned)rs->rects,
               (unsigned)rs->pixels, (unsigned)rs->us, (unsigned)rs->max_us);
#endif
    } else {
        printf("Send YYYY-MM-DD HH:MM:SS to set the clock, ? to show it\n");
    }
    return false;
}

// Non-blocking line reader on USB serial. Returns true if the clock was set.
static bool poll_usb(void) {
    static char line[40];
    static size_t len;
    bool changed = false;
    int c;

    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r' || c == '\n') {
            if (len) {
                putchar('\n');
                line[len] = '\0';
                changed |= handle_line(line);
                len = 0;
            }
        } else if (len < sizeof line - 1) {
            line[len++] = (char)c;
            putchar(c); // echo, most terminals don't
        }
    }
    return changed;
}

static void draw_centered(int y, const char *s, uint16_t fg, uint16_t bg, int scale) {
    int w = (int)strlen(s) * 6 * scale;
    ili9341_draw_string((ILI9341_WIDTH - w) / 2, y, s, fg, bg, scale);
}

// Draws only the characters of `now` that differ from what is on screen.
static void update_text(int y, const char *now, char *shown, uint16_t fg, int scale) {
    int x = (ILI9341_WIDTH - (int)strlen(now) * 6 * scale) / 2;
    for (size_t i = 0; now[i]; i++, x += 6 * scale) {
        if (now[i] != shown[i]) {
            ili9341_draw_char(x, y, now[i], fg, COLOR_BLACK, scale);
            shown[i] = now[i];
        }
    }
}

static void draw_clock(const datetime_t *t) {
    char time_str[16], date_str[16], day_str[16];

    snprintf(time_str, sizeof time_str, "%02d:%02d:%02d", t->hour, t->min, t->sec);
    snprintf(date_str, sizeof date_str, "%02d %s %04d", t->day, MONTH_NAMES[t->month - 1], t->year);

    // Pad the day name to a fixed, centred field so shorter names erase longer ones.
    const char *name = DAY_NAMES[t->dotw];
    int pad = (DAY_FIELD - (int)strlen(name)) / 2;
    snprintf(day_str, sizeof day_str, "%*s%-*s", pad, "", DAY_FIELD - pad, name);

    update_text(TIME_Y, time_str, shown_time, COLOR_WHITE, TIME_SCALE);
    update_text(DATE_Y, date_str, shown_date, COLOR_CYAN, DATE_SCALE);
    update_text(DAY_Y, day_str, shown_day, COLOR_ORANGE, DAY_SCALE);
}

// Each screen is labelled with the colour it should be. If the label says RED
// but the screen is blue, toggle the BGR bit in ILI9341_MADCTL.
static void colour_test(void) {
    static const struct {
        uint16_t color;
        const char *name;
    } steps[] = {
        {COLOR_RED, "RED"}, {COLOR_GREEN, "GREEN"}, {COLOR_BLUE, "BLUE"}, {COLOR_WHITE, "WHITE"},
    };
    for (unsigned i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        ili9341_fill_screen(steps[i].color);
        draw_centered(108, steps[i].name, COLOR_BLACK, steps[i].color, 4);
        sleep_ms(600);
    }
}

#if SHOW_COLOUR_BARS && !SHOW_DRAGON && !SHOW_TORTOISE
// Eight full-height bars: white, yellow, cyan, green, magenta, red, blue, black.
static void colour_bars(void) {
    static const uint16_t bars[] = {
        COLOR_WHITE, COLOR_YELLOW, COLOR_CYAN, COLOR_GREEN,
        RGB565(255, 0, 255), COLOR_RED, COLOR_BLUE, COLOR_BLACK,
    };
    int n = sizeof bars / sizeof bars[0];
    for (int i = 0; i < n; i++) {
        int x0 = i * ILI9341_WIDTH / n, x1 = (i + 1) * ILI9341_WIDTH / n;
        ili9341_fill_rect(x0, 0, x1 - x0, ILI9341_HEIGHT, bars[i]);
    }
}
#endif

int main(void) {
    stdio_init_all();
    rtc_init();
    set_clock_from_build_time();

    const ili9341_config_t cfg = {
        .spi = TFT_SPI,
        .pin_sck = PIN_SCK,
        .pin_mosi = PIN_MOSI,
        .pin_miso = PIN_MISO,
        .pin_cs = PIN_CS,
        .pin_dc = PIN_DC,
        .pin_rst = PIN_RST,
        .pin_bl = PIN_BL,
        .baud = SPI_BAUD,
    };
    uint baud = spi_baud = ili9341_init(&cfg);
    uint8_t power_mode = ili9341_read_reg8(ILI9341_RDDPM);
    printf("ILI9341 clock: SPI %u Hz, RDDPM 0x%02X (expect 0x9C)\n", baud, power_mode);

    colour_test();

#if SHOW_TORTOISE
    tortoise_draw_scene();
    input_init();

    // Fixed timestep: game ticks are scheduled on an absolute 1/GAME_FPS grid, so
    // a slow frame never shifts later ones. After a long stall (more than
    // MAX_CATCH_UP ticks behind) the grid is re-anchored instead of fast-forwarding.
    const uint64_t tick_us = 1000000 / GAME_FPS;
    const int max_catch_up = 3;
    uint64_t next_tick = time_us_64() + tick_us;
    while (true) {
        uint64_t now = time_us_64();
        input_poll((uint32_t)(now / 1000));

        int ticks = 0;
        while (now >= next_tick && ticks < max_catch_up) {
            tortoise_buttons(input_pressed());
            tortoise_step();
            next_tick += tick_us;
            ticks++;
        }
        if (ticks) {
            if (now >= next_tick) next_tick = now + tick_us;
            render_flush();
        }

        static uint64_t next_usb;
        if (now >= next_usb) { // keep serial alive so '?' and 1200-baud flashing still work
            poll_usb();
            next_usb = now + 10000;
        }
    }
#elif SHOW_DRAGON
    dragon_draw_scene();
    while (true) {
        poll_usb(); // keep serial alive so '?' and 1200-baud flashing still work
        dragon_step(time_us_32() / 1000);
        sleep_ms(10);
    }
#elif SHOW_COLOUR_BARS
    colour_bars();
    while (true) {
        poll_usb(); // keep serial alive so '?' and 1200-baud flashing still work
        sleep_ms(10);
    }
#endif

    char info[40];
    ili9341_fill_screen(COLOR_BLACK);
    draw_centered(12, "RP2040 + ILI9341", COLOR_YELLOW, COLOR_BLACK, 2);
    snprintf(info, sizeof info, "SPI %u.%02u MHz  RDDPM 0x%02X", baud / 1000000,
             (baud % 1000000) / 10000, power_mode);
    draw_centered(212, info, COLOR_GREY, COLOR_BLACK, 1);
    draw_centered(226, "USB: send YYYY-MM-DD HH:MM:SS to set", COLOR_GREY, COLOR_BLACK, 1);

    int last_sec = -1;
    while (true) {
        if (poll_usb()) last_sec = -1;

        datetime_t now;
        rtc_get_datetime(&now);
        if (now.sec != last_sec) {
            last_sec = now.sec;
            draw_clock(&now);
        }
        sleep_ms(10);
    }
}
