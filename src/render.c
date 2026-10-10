#include "render.h"

#include <string.h>

#include "font5x7.h"
#include "ili9341.h"
#include "pico/stdlib.h"

#define RECT_MAX    48
#define MAX_LAYERS  6
#define STRIP_PIX   4096 // two strips of 8 KB; a 320 px wide rect is sent 12 rows at a time
#define MERGE_SLACK 128  // merge near-touching rects if that costs this few extra pixels

typedef struct {
    int x0, y0, x1, y1; // half-open: x0 <= x < x1
} rect_t;

static const uint16_t *bg;
static render_layer_fn layers[MAX_LAYERS];
static int n_layers;
static rect_t rects[RECT_MAX];
static int n_rects;
static uint16_t strip[2][STRIP_PIX];
static render_stats_t stats;

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
    if (r.x1 > ILI9341_WIDTH) r.x1 = ILI9341_WIDTH;
    if (r.y1 > ILI9341_HEIGHT) r.y1 = ILI9341_HEIGHT;
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
    rects[n_rects++] = (rect_t){0, 0, ILI9341_WIDTH, ILI9341_HEIGHT};
}

void render_compose(const render_target_t *t) {
    for (int row = 0; row < t->h; row++)
        memcpy(&t->buf[row * t->w], &bg[(t->y + row) * ILI9341_WIDTH + t->x], (size_t)t->w * 2);
    for (int i = 0; i < n_layers; i++) layers[i](t);
}

void render_flush(void) {
    uint32_t t0 = time_us_32();
    uint32_t pixels = 0;
    int cur = 0;

    for (int i = 0; i < n_rects; i++) {
        rect_t r = rects[i];
        int w = r.x1 - r.x0, rows = STRIP_PIX / w;
        pixels += (uint32_t)area(r);
        for (int y = r.y0; y < r.y1; y += rows) {
            render_target_t t = {strip[cur], r.x0, y, w, r.y1 - y < rows ? r.y1 - y : rows};
            render_compose(&t);
            ili9341_blit_dma(t.x, t.y, t.w, t.h, t.buf); // waits for the previous strip first
            cur ^= 1;
        }
    }
    ili9341_wait();

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
