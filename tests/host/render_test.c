// Renderer and game test. Drives render.c, input.c and the tortoise game against
// the emulated panel and, after every flushed frame, checks that the panel holds
// exactly what a full-screen composite of background + layers would give. That
// catches any area that changed without being reported dirty.

#define EMU_NO_SCENARIO
#include "emulator.c"

#include "input.h"
#include "render.h"
#include "tortoise.h"

extern const uint16_t meadow_image[GAME_W * GAME_H];
extern uint64_t emu_time_us;
extern uint32_t emu_pins_low;

void sleep_ms(uint32_t ms) { (void)ms; }

// The game canvas (CW x CH) is shown on the panel (W x H) scaled 4/3.
#define CW GAME_W
#define CH GAME_H

static uint16_t canvas[CH * CW];

// Panel must equal the full composite, scaled as render.c scales it. Returns the
// number of differing pixels.
static int compare_screen(void) {
    render_target_t t = {canvas, 0, 0, CW, CH};
    render_compose(&t);
    int bad = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            bad += lcd.fb[y][x] != canvas[(3 * y / 4) * CW + 3 * x / 4];
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
    render_init(meadow_image);
    render_add_layer(squares_layer);
    srand(7);
    for (int i = 0; i < N_SQ; i++)
        sq[i] = (typeof(sq[i])){rand() % (CW + 20) - 10, rand() % (CH + 20) - 10,
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
            if (sq[i].x < -9 || sq[i].x > CW) sq[i].dx = -sq[i].dx;
            if (sq[i].y < -9 || sq[i].y > CH) sq[i].dy = -sq[i].dy;
            render_dirty(sq[i].x, sq[i].y, 9, 9);
        }
        render_flush();
        px_total += render_stats()->pixels;
        int bad = compare_screen();
        CHECK(bad == 0, "stress frame %d: %d pixels differ", frame, bad);
        if (bad) break;
        CHECK(render_stats()->rects <= 48, "too many rects");
    }
    printf("stress: 40 objects, avg %lu px/frame redrawn (full screen = %d)\n", px_total / 200, CW * CH);
}

// ---- the game

// Button script: {first tick, last tick, GPIO}. Covers aiming both ways, walking
// both ways, a quick tap, a full charge, and RESET.
static const struct { int from, to, pin; } script[] = {
    {5, 30, 26},    // UP: aim up to the limit
    {40, 45, 14},   // CENTER tap: weak shot
    {50, 70, 27},   // DOWN
    {80, 140, 15},  // RIGHT: walk right
    {100, 160, 14}, // CENTER: charge to full and release at 160
    {170, 200, 28}, // LEFT
    {180, 190, 14}, // CENTER while walking
    {210, 400, 26}, // UP: pin at the maximum angle
    {250, 275, 14}, // CENTER: charge, high angle
    {300, 380, 15}, // RIGHT to the end stop
    {400, 600, 27}, // DOWN: pin at the minimum angle
    {420, 460, 14}, {470, 480, 14}, {490, 530, 14},
    {600, 602, 1},  // RESET
    {620, 700, 14}, // CENTER after reset
    {720, 760, 28}, // LEFT
};

static void game_test(void) {
    tortoise_draw_scene();
    CHECK(compare_screen() == 0, "game: first frame mismatch");
    input_init();

    unsigned long px_total = 0, frames_over = 0;
    uint32_t worst_rects = 0;
    for (int tick = 0; tick < 900; tick++) {
        emu_time_us += 1000000 / 30;
        emu_pins_low = 0;
        for (unsigned i = 0; i < sizeof script / sizeof script[0]; i++)
            if (tick >= script[i].from && tick <= script[i].to) emu_pins_low |= 1u << script[i].pin;
        input_poll((uint32_t)(emu_time_us / 1000));
        input_poll((uint32_t)(emu_time_us / 1000) + 20);

        tortoise_step(input_held(), input_pressed(), input_released());
        render_flush();

        const render_stats_t *rs = render_stats();
        px_total += rs->pixels;
        if (rs->pixels > CW * CH / 4) frames_over++;
        if (rs->rects > worst_rects) worst_rects = rs->rects;
        int bad = compare_screen();
        CHECK(bad == 0, "game tick %d: %d pixels differ", tick, bad);
        if (bad) break;
        if (tick == 150 || tick == 265 || tick == 340) {
            char name[32];
            snprintf(name, sizeof name, "game_tick%d", tick);
            save_ppm(name);
        }
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
    const ili9488_config_t cfg = {.spi = spi0, .pin_sck = PIN_SCK, .pin_mosi = PIN_MOSI,
                                  .pin_miso = PIN_MISO, .pin_cs = PIN_CS, .pin_dc = PIN_DC,
                                  .pin_rst = PIN_RST, .pin_bl = PIN_BL, .baud = SPI_BAUD};
    ili9488_init(&cfg);
    input_test();
    stress_test();
    game_test();
    CHECK(lcd.writes_without_cs == 0, "%d SPI writes with CS high", lcd.writes_without_cs);
    CHECK(lcd.pixels_out_of_window == 0, "%d pixels outside window", lcd.pixels_out_of_window);
    printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
