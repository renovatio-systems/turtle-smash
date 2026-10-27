// A side-on dragon breathing fire to the right. The dragon is drawn once from
// filled shapes; only the flame area is redrawn each frame (classic fire
// effect: a heat map that cools and blurs as it moves away from the mouth).

#include <stdlib.h>

#include "dragon.h"
#include "ili9488.h"

#define SKY       COLOR_BLACK
#define GROUND    RGB565(60, 40, 25)
#define GREEN     RGB565(40, 160, 60)
#define DKGREEN   RGB565(20, 100, 40)
#define BELLY     RGB565(200, 190, 90)
#define MEMBRANE  RGB565(150, 30, 40)
#define BONE      RGB565(90, 15, 25)
#define HORN      RGB565(230, 220, 180)
#define MOUTH     RGB565(180, 20, 20)
#define EYE       RGB565(255, 220, 0)

// Flame area, to the right of the mouth. MOUTH_ROW is the mouth's row inside it.
#define FIRE_X    184
#define FIRE_Y    57
#define FIRE_W    136
#define FIRE_H    100
#define MOUTH_ROW 50

// ------------------------------------------------------------ shape helpers

static void fill_circle(int cx, int cy, int r, uint16_t c) {
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) <= r * r - dy * dy) dx++;
        ili9488_fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}

static void fill_tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    const int px[3] = {x0, x1, x2}, py[3] = {y0, y1, y2};
    int ymin = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int ymax = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    for (int y = ymin; y <= ymax; y++) {
        int lo = 1000, hi = -1000;
        for (int e = 0; e < 3; e++) {
            int xa = px[e], ya = py[e], xb = px[(e + 1) % 3], yb = py[(e + 1) % 3];
            if (ya == yb) {
                if (y != ya) continue;
                if (xa < lo) lo = xa;
                if (xb < lo) lo = xb;
                if (xa > hi) hi = xa;
                if (xb > hi) hi = xb;
            } else if ((y >= ya && y <= yb) || (y >= yb && y <= ya)) {
                int x = xa + (y - ya) * (xb - xa) / (yb - ya);
                if (x < lo) lo = x;
                if (x > hi) hi = x;
            }
        }
        if (hi >= lo) ili9488_fill_rect(lo, y, hi - lo + 1, 1, c);
    }
}

// A line made of circles whose radius runs from r0 to r1.
static void thick_line(int x0, int y0, int x1, int y1, int r0, int r1, uint16_t c) {
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int n = (dx > dy ? dx : dy) / 2 + 1;
    for (int i = 0; i <= n; i++)
        fill_circle(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, r0 + (r1 - r0) * i / n, c);
}

// ------------------------------------------------------------------- scene

void dragon_draw_scene(void) {
    ili9488_fill_screen(SKY);
    ili9488_fill_rect(0, 212, ILI9488_WIDTH, 28, GROUND);
    ili9488_fill_rect(0, 212, ILI9488_WIDTH, 2, RGB565(110, 80, 50));

    // Stars, kept out of the flame area (which is repainted every frame).
    uint32_t s = 12345;
    for (int i = 0; i < 30; i++) {
        s = s * 1664525u + 1013904223u;
        int x = (s >> 8) % FIRE_X, y = (s >> 20) % 200;
        ili9488_fill_rect(x, y, 1, 1, (i & 1) ? COLOR_WHITE : COLOR_GREY);
    }

    // Far wing, then near wing.
    fill_tri(110, 140, 100, 60, 130, 110, BONE);
    fill_tri(95, 140, 45, 45, 60, 100, MEMBRANE);
    fill_tri(95, 140, 60, 100, 85, 115, MEMBRANE);
    fill_tri(95, 140, 85, 115, 120, 128, MEMBRANE);
    thick_line(95, 140, 45, 45, 1, 1, BONE);
    thick_line(95, 140, 60, 100, 1, 1, BONE);
    thick_line(95, 140, 85, 115, 1, 1, BONE);

    // Tail, ending in a spade.
    thick_line(70, 180, 30, 205, 18, 8, GREEN);
    thick_line(30, 205, 8, 186, 8, 2, GREEN);
    fill_tri(8, 186, 0, 168, 16, 176, MEMBRANE);

    // Legs and feet.
    ili9488_fill_rect(70, 192, 16, 20, DKGREEN);
    ili9488_fill_rect(65, 208, 28, 6, DKGREEN);
    ili9488_fill_rect(115, 192, 14, 20, DKGREEN);
    ili9488_fill_rect(112, 208, 26, 6, DKGREEN);

    // Body and belly.
    fill_circle(95, 175, 34, GREEN);
    fill_circle(100, 188, 18, BELLY);
    for (int i = 0; i < 3; i++)
        fill_tri(70 + i * 12, 148 - i * 3, 74 + i * 12, 138 - i * 3, 80 + i * 12, 146 - i * 3, MEMBRANE);

    // Neck with back spikes.
    thick_line(115, 155, 145, 112, 16, 12, GREEN);
    static const int spike[5][2] = {{98, 146}, {108, 134}, {118, 122}, {127, 111}, {133, 100}};
    for (int i = 0; i < 5; i++) {
        int x = spike[i][0], y = spike[i][1];
        fill_tri(x, y, x - 6, y - 9, x + 5, y - 5, MEMBRANE);
    }

    // Head: horns, skull, lower jaw, open mouth, upper jaw, teeth, eye.
    fill_tri(142, 92, 126, 70, 150, 90, HORN);
    fill_tri(150, 90, 138, 68, 156, 88, HORN);
    fill_circle(152, 104, 15, GREEN);
    ili9488_fill_rect(152, 110, 26, 8, DKGREEN);
    ili9488_fill_rect(156, 105, 28, 5, MOUTH);
    ili9488_fill_rect(152, 94, 32, 11, GREEN);
    for (int x = 160; x <= 178; x += 6) {
        fill_tri(x, 105, x + 3, 105, x + 1, 109, COLOR_WHITE);
    }
    ili9488_fill_rect(180, 97, 2, 2, DKGREEN); // nostril
    fill_circle(150, 99, 4, EYE);
    ili9488_fill_rect(150, 96, 2, 6, COLOR_BLACK);
    thick_line(142, 94, 156, 95, 1, 1, DKGREEN); // brow
}

// -------------------------------------------------------------------- fire

static uint8_t heat[FIRE_H][FIRE_W];
static uint16_t fire_px[FIRE_H * FIRE_W];
static uint16_t palette[256];
static uint32_t rng = 2463534242u;

static uint32_t rnd(void) { // xorshift32
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void build_palette(void) {
    for (int i = 0; i < 256; i++) { // black -> red -> yellow -> white
        int r = i * 3 > 255 ? 255 : i * 3;
        int g = i < 85 ? 0 : ((i - 85) * 3 > 255 ? 255 : (i - 85) * 3);
        int b = i < 170 ? 0 : ((i - 170) * 3 > 255 ? 255 : (i - 170) * 3);
        palette[i] = RGB565(r, g, b);
    }
}

// 0..255: how hard the dragon is breathing. Inhale 1.2 s, then a fierce blast.
static int breath_strength(uint32_t ms) {
    uint32_t t = ms % 4000;
    if (t < 1200) return 0;
    if (t < 1500) return (int)((t - 1200) * 255 / 300);
    if (t < 3200) return 255;
    return 0;
}

void dragon_step(uint32_t ms) {
    static bool ready;
    if (!ready) {
        build_palette();
        ready = true;
    }

    int strength = breath_strength(ms);
    for (int y = 0; y < FIRE_H; y++) {
        int d = abs(y - MOUTH_ROW);
        heat[y][0] = (d <= 5 && strength) ? (uint8_t)((200 + (rnd() & 55)) * strength / 255) : 0;
    }

    // Flames travel right: each cell averages the cells to its left (already
    // updated this frame, so the jet spans the whole width), taking a little
    // from below so the fire drifts upwards, and cools by a random amount.
    for (int x = 1; x < FIRE_W; x++) {
        for (int y = 0; y < FIRE_H; y++) {
            int up = y > 0 ? y - 1 : 0;
            int down = y + 1 < FIRE_H ? y + 1 : FIRE_H - 1;
            int sum = heat[y][x - 1] * 2 + heat[up][x - 1] + heat[down][x - 1];
            int v = sum / 4 - (int)(rnd() & 1);
            heat[y][x] = v > 0 ? v : 0;
        }
    }

    for (int y = 0; y < FIRE_H; y++)
        for (int x = 0; x < FIRE_W; x++)
            fire_px[y * FIRE_W + x] = palette[heat[y][x]];
    ili9488_blit(FIRE_X, FIRE_Y, FIRE_W, FIRE_H, fire_px);
}
