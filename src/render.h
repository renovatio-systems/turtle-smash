#ifndef RENDER_H
#define RENDER_H

#include <stdbool.h>
#include <stdint.h>

// Dirty-rectangle renderer. The background is a static 320x240 RGB565 image in
// flash. Game code reports every screen area that changed (render_dirty); at the
// end of each tick render_flush() merges those rectangles and, for each one,
// composites background + layers into a small RAM strip and sends it over SPI
// by DMA (one strip is composed while the previous one is being sent).

// A block of the screen being composited: pixel (sx, sy) is buf[(sy - y) * w + (sx - x)].
typedef struct {
    uint16_t *buf;
    int x, y, w, h;
} render_target_t;

// A layer draws everything it owns that overlaps the target, clipped to it.
typedef void (*render_layer_fn)(const render_target_t *t);

void render_init(const uint16_t *background);
void render_add_layer(render_layer_fn fn); // layers are drawn in the order added

void render_dirty(int x, int y, int w, int h); // clipped to the screen
void render_dirty_all(void);
void render_flush(void);

// Composites background + all layers for any in-bounds block (flush does this per strip).
void render_compose(const render_target_t *t);

// Layer helpers.
static inline uint16_t *render_px(const render_target_t *t, int sx, int sy) {
    return &t->buf[(sy - t->y) * t->w + (sx - t->x)];
}
// Intersects the box with the target; returns false if they do not overlap.
bool render_clip(const render_target_t *t, int x, int y, int w, int h, int *x0, int *y0, int *x1, int *y1);
// Fills a box (clipped to the target).
void render_fill(const render_target_t *t, int x, int y, int w, int h, uint16_t color);
// Draws text in the lib's 5x7 font (cell 6*scale x 8*scale, background included).
void render_text(const render_target_t *t, int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale);

typedef struct {
    uint32_t rects, pixels; // last flush, after merging
    uint32_t us, max_us;    // last / worst flush time
} render_stats_t;
const render_stats_t *render_stats(void);

#endif
