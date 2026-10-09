// Renderer and game test. Drives render.c, input.c and the tortoise game against
// the emulated panel and, after every flushed frame, checks that the panel holds
// exactly what a full-screen composite of background + layers would give. That
// catches any area that changed without being reported dirty.

#define EMU_NO_SCENARIO
#include "emulator.c"

#include "input.h"
#include "render.h"
#include "tortoise.h"

extern const uint16_t tortoise_image[ILI9341_WIDTH * ILI9341_HEIGHT];
extern uint64_t emu_time_us;
extern uint32_t emu_pins_low;

void sleep_ms(uint32_t ms) { (void)ms; }

static uint16_t expect[H * W];

// Panel must equal the full composite; returns the number of differing pixels.
static int compare_screen(void) {
    render_target_t t = {expect, 0, 0, W, H};
    render_compose(&t);
    int bad = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            bad += lcd.fb[y][x] != expect[y * W + x];
    return bad;
}

// ---- synthetic stress: 40 small moving squares + a fat one

#define N_SQ 40
static struct { int x, y, dx, dy; uint16_t c; } sq[N_SQ];

static void squares_layer(const render_target_t *t) {
    for (int i = 0; i < N_SQ; i++) {
        int x0, y0, x1, y1;
        if (!render_clip(t, sq[i].x, sq[i].y, 9, 9, &x0, &y0, &x1, &y1)) continue;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) *render_px(t, x, y) = sq[i].c;
    }
}

static void stress_test(void) {
    render_init(tortoise_image);
    render_add_layer(squares_layer);
    srand(7);
    for (int i = 0; i < N_SQ; i++)
        sq[i] = (typeof(sq[i])){rand() % (W + 20) - 10, rand() % (H + 20) - 10,
                                rand() % 9 - 4, rand() % 9 - 4, (uint16_t)(rand() | 0x0841)};
    render_dirty_all();
    render_flush();
    CHECK(compare_screen() == 0, "stress: first frame mismatch");

    unsigned long px_total = 0;
    for (int frame = 0; frame < 200; frame++) {
        for (int i = 0; i < N_SQ; i++) {
            render_dirty(sq[i].x, sq[i].y, 9, 9);
            sq[i].x += sq[i].dx;
            sq[i].y += sq[i].dy;
            if (sq[i].x < -9 || sq[i].x > W) sq[i].dx = -sq[i].dx;
            if (sq[i].y < -9 || sq[i].y > H) sq[i].dy = -sq[i].dy;
            render_dirty(sq[i].x, sq[i].y, 9, 9);
        }
        render_flush();
        px_total += render_stats()->pixels;
        int bad = compare_screen();
        CHECK(bad == 0, "stress frame %d: %d pixels differ", frame, bad);
        if (bad) break;
        CHECK(render_stats()->rects <= 48, "too many rects");
    }
    printf("stress: 40 objects, avg %lu px/frame redrawn (full screen = %d)\n", px_total / 200, W * H);
}

// ---- the game

static const struct { int pin; } script_pins[] = {{26}, {27}, {28}, {15}, {14}};

static void game_test(void) {
    tortoise_draw_scene();
    CHECK(compare_screen() == 0, "game: first frame mismatch");
    input_init();

    unsigned long px_total = 0, frames_over = 0;
    uint32_t worst_rects = 0;
    for (int tick = 0; tick < 900; tick++) {
        emu_time_us += 1000000 / 30;
        emu_pins_low = 0;
        if (tick >= 5 && tick < 9) emu_pins_low |= 1u << 0; // SET: auto-fire on
        if (tick % 25 < 4) emu_pins_low |= 1u << script_pins[(tick / 25) % 5].pin;
        if (tick == 600 || tick == 601 || tick == 602) emu_pins_low |= 1u << 1; // RESET
        input_poll((uint32_t)(emu_time_us / 1000));
        input_poll((uint32_t)(emu_time_us / 1000) + 20);

        tortoise_buttons(input_pressed());
        tortoise_step();
        render_flush();

        const render_stats_t *rs = render_stats();
        px_total += rs->pixels;
        if (rs->pixels > W * H / 4) frames_over++;
        if (rs->rects > worst_rects) worst_rects = rs->rects;
        int bad = compare_screen();
        CHECK(bad == 0, "game tick %d: %d pixels differ", tick, bad);
        if (bad) break;
    }
    printf("game: 900 ticks, avg %lu px/frame, %lu frames over a quarter screen, max %u rects\n",
           px_total / 900, frames_over, (unsigned)worst_rects);
    save_ppm("game_screen");
}

static void input_test(void) {
    input_init();
    emu_pins_low = 1u << 14; // CENTER pressed
    emu_time_us = 1000000;
    input_poll(1000);
    CHECK(!(input_held() & JOY_MID), "pressed too early");
    emu_pins_low = 0; // bounce back inside the debounce window
    input_poll(1005);
    emu_pins_low = 1u << 14;
    input_poll(1008);
    input_poll(1020);
    CHECK(!(input_held() & JOY_MID), "bounce counted as a press");
    input_poll(1030);
    CHECK(input_held() & JOY_MID, "press not registered");
    CHECK(input_pressed() == JOY_MID && input_pressed() == 0, "press edge not reported once");
    emu_pins_low = 0;
    input_poll(1100);
    input_poll(1120);
    CHECK(!(input_held() & JOY_MID) && input_released() == JOY_MID, "release not reported");
}

int main(void) {
    const ili9341_config_t cfg = {.spi = spi0, .pin_sck = PIN_SCK, .pin_mosi = PIN_MOSI,
                                  .pin_miso = PIN_MISO, .pin_cs = PIN_CS, .pin_dc = PIN_DC,
                                  .pin_rst = PIN_RST, .pin_bl = PIN_BL, .baud = SPI_BAUD};
    ili9341_init(&cfg);
    input_test();
    stress_test();
    game_test();
    CHECK(lcd.writes_without_cs == 0, "%d SPI writes with CS high", lcd.writes_without_cs);
    CHECK(lcd.pixels_out_of_window == 0, "%d pixels outside window", lcd.pixels_out_of_window);
    printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
