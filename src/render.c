#include "render.h"

#include <string.h>

#include "font5x7.h"
#include "ili9488.h"
#include "pico/stdlib.h"

#define RECT_MAX    48
#define MAX_LAYERS  6
#define MERGE_SLACK 128 // merge near-touching rects if that costs this few extra pixels

// Canvas rows composed at a time. Four canvas rows cover at most six panel rows.
#define BLOCK_ROWS      4
#define BLOCK_PANEL_ROWS 8
#define PANEL_BLOCK_BYTES (BLOCK_PANEL_ROWS * ILI9488_WIDTH * ILI9488_BYTES_PER_PIXEL)

typedef struct {
    int x0, y0, x1, y1; // half-open: x0 <= x < x1
} rect_t;

static const uint16_t *bg;
static render_layer_fn layers[MAX_LAYERS];
static int n_layers;
static rect_t rects[RECT_MAX];
static int n_rects;
static uint16_t canvas_block[BLOCK_ROWS * GAME_W];
static uint8_t panel_block[2][PANEL_BLOCK_BYTES]; // one is expanded while the other is sent
static render_stats_t stats;

// The canvas is scaled 4/3: panel pixel p shows canvas pixel 3p/4, and canvas
// pixel c starts at panel pixel ceil(4c/3).
static int canvas_of_panel(int p) { return p * 3 / 4; }
static int panel_of_canvas(int c) { return (4 * c + 2) / 3; }

static int area(rect_t r) { return (r.x1 - r.x0) * (r.y1 - r.y0); }

static rect_t unite(rect_t a, rect_t b) {
    return (rect_t){a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0,
                    a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1};
}

static bool overlap(rect_t a, rect_t b) {
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

// Merging pays when the union is not much bigger than the two parts: overlapping
// rects almost always qualify, a diagonal pair whose bounding box is mostly empty
// does not and stays separate (the small overlap is then simply drawn twice).
static bool worth_merging(rect_t a, rect_t b) {
    int sum = area(a) + area(b), u = area(unite(a, b));
    return overlap(a, b) ? u <= sum + sum / 2 + MERGE_SLACK : u <= sum + MERGE_SLACK;
}

void render_init(const uint16_t *background) {
    bg = background;
    n_rects = n_layers = 0;
}

void render_add_layer(render_layer_fn fn) {
    if (n_layers < MAX_LAYERS) layers[n_layers++] = fn;
}

void render_dirty(int x, int y, int w, int h) {
    rect_t r = {x, y, x + w, y + h};
    if (r.x0 < 0) r.x0 = 0;
    if (r.y0 < 0) r.y0 = 0;
    if (r.x1 > GAME_W) r.x1 = GAME_W;
    if (r.y1 > GAME_H) r.y1 = GAME_H;
    if (r.x0 >= r.x1 || r.y0 >= r.y1) return;

    for (;;) {
        int best = -1, best_grow = 0;
        for (int i = 0; i < n_rects; i++) {
            if (worth_merging(r, rects[i])) {
                best = i;
                break;
            }
        }
        if (best < 0 && n_rects == RECT_MAX) { // list full: absorb into the cheapest neighbour
            for (int i = 0; i < n_rects; i++) {
                int grow = area(unite(r, rects[i])) - area(rects[i]);
                if (best < 0 || grow < best_grow) best = i, best_grow = grow;
            }
        }
        if (best < 0) break;
        r = unite(r, rects[best]);
        rects[best] = rects[--n_rects];
    }
    rects[n_rects++] = r;
}

void render_dirty_all(void) {
    n_rects = 0;
    rects[n_rects++] = (rect_t){0, 0, GAME_W, GAME_H};
}

void render_compose(const render_target_t *t) {
    for (int row = 0; row < t->h; row++)
        memcpy(&t->buf[row * t->w], &bg[(t->y + row) * GAME_W + t->x], (size_t)t->w * 2);
    for (int i = 0; i < n_layers; i++) layers[i](t);
}

void render_flush(void) {
    uint32_t t0 = time_us_32();
    uint32_t pixels = 0;
    int cur = 0;

    for (int i = 0; i < n_rects; i++) {
        rect_t r = rects[i];
        int w = r.x1 - r.x0;
        int px0 = panel_of_canvas(r.x0), px1 = panel_of_canvas(r.x1);
        pixels += (uint32_t)area(r);
        for (int y = r.y0; y < r.y1; y += BLOCK_ROWS) {
            int rows = r.y1 - y < BLOCK_ROWS ? r.y1 - y : BLOCK_ROWS;
            render_target_t t = {canvas_block, r.x0, y, w, rows};
            render_compose(&t);

            // Expand the composed canvas rows to the panel rows they cover.
            int py0 = panel_of_canvas(y), py1 = panel_of_canvas(y + rows);
            uint8_t *out = panel_block[cur];
            for (int py = py0; py < py1; py++) {
                const uint16_t *src = &canvas_block[(canvas_of_panel(py) - y) * w];
                for (int px = px0; px < px1; px++, out += ILI9488_BYTES_PER_PIXEL)
                    ili9488_pack(src[canvas_of_panel(px) - r.x0], out);
            }
            ili9488_blit_dma(px0, py0, px1 - px0, py1 - py0, panel_block[cur]); // waits for the previous block first
            cur ^= 1;
        }
    }
    ili9488_wait();

    stats.rects = (uint32_t)n_rects;
    stats.pixels = pixels;
    stats.us = time_us_32() - t0;
    if (stats.us > stats.max_us && n_rects) stats.max_us = stats.us;
    n_rects = 0;
}

const render_stats_t *render_stats(void) { return &stats; }

bool render_clip(const render_target_t *t, int x, int y, int w, int h, int *x0, int *y0, int *x1, int *y1) {
    *x0 = x > t->x ? x : t->x;
    *y0 = y > t->y ? y : t->y;
    *x1 = x + w < t->x + t->w ? x + w : t->x + t->w;
    *y1 = y + h < t->y + t->h ? y + h : t->y + t->h;
    return *x0 < *x1 && *y0 < *y1;
}

void render_fill(const render_target_t *t, int x, int y, int w, int h, uint16_t color) {
    int x0, y0, x1, y1;
    if (!render_clip(t, x, y, w, h, &x0, &y0, &x1, &y1)) return;
    for (int sy = y0; sy < y1; sy++) {
        uint16_t *p = render_px(t, x0, sy);
        for (int sx = x0; sx < x1; sx++) *p++ = color;
    }
}

void render_text(const render_target_t *t, int x, int y, const char *s, uint16_t fg, uint16_t bg_col, int scale) {
    int cw = 6 * scale, ch = 8 * scale;
    for (; *s; s++, x += cw) {
        int x0, y0, x1, y1;
        if (!render_clip(t, x, y, cw, ch, &x0, &y0, &x1, &y1)) continue;
        const uint8_t *glyph = font5x7_glyph(*s);
        for (int sy = y0; sy < y1; sy++) {
            int gy = (sy - y) / scale;
            uint16_t *p = render_px(t, x0, sy);
            for (int sx = x0; sx < x1; sx++) {
                int gx = (sx - x) / scale;
                *p++ = (gx < 5 && gy < 7 && (glyph[gx] >> gy) & 1) ? fg : bg_col;
            }
        }
    }
}
