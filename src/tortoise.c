// The armoured tortoise picture (src/tortoise_image.c, generated from
// assets/tortoise.png) with cannonballs fired from its cannon. The picture is
// drawn once; each frame only small tiles around moving objects are redrawn,
// composed from the picture plus the objects on top of it.

#include <stdbool.h>
#include <stdlib.h>

#include "ili9341.h"
#include "joystick.h"
#include "tortoise.h"

extern const uint16_t tortoise_image[ILI9341_WIDTH * ILI9341_HEIGHT];

// Muzzle of the cannon and the grass line, in screen pixels.
#define MUZZLE_X  146
#define MUZZLE_Y  85
#define GROUND_Y  206

#define FRAME_MS  25
#define MAX_OBJS  12
#define TILE_MAX  48
#define FP        8 // fixed point: 8 fractional bits
#define ONE       (1 << FP)
#define GRAVITY   20 // 0.08 px/frame^2

enum { BALL, FLASH, DUST };

typedef struct {
    bool active;
    int kind;
    int x, y, vx, vy; // fixed point
    int g;            // gravity added to vy each frame
    int r, r0, life, life0;
} obj_t;

static obj_t objs[MAX_OBJS];
static bool auto_fire = true;
static uint16_t tile[TILE_MAX * TILE_MAX];
static uint32_t rng = 2463534242u;

static uint32_t rnd(void) { // xorshift32
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int rnd_range(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }

// Colour of object `o` at offset (dx, dy) from its centre, or 0 if not covered.
static bool obj_pixel(const obj_t *o, int dx, int dy, uint16_t *c) {
    int d2 = dx * dx + dy * dy, r2 = o->r * o->r;
    if (d2 > r2) return false;
    switch (o->kind) {
    case BALL: {
        int h = o->r / 3 + 1, hx = dx + h, hy = dy + h;
        *c = (hx * hx + hy * hy <= h * h) ? COLOR_WHITE : RGB565(140, 140, 155);
        return true;
    }
    case FLASH:
        *c = d2 * 3 < r2 ? COLOR_WHITE : (d2 * 3 < 2 * r2 ? COLOR_YELLOW : COLOR_ORANGE);
        return true;
    default:
        *c = RGB565(120, 100, 70);
        return true;
    }
}

// Redraws a rectangle: the picture with every active object on top.
static void redraw(int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > ILI9341_WIDTH - 1) x1 = ILI9341_WIDTH - 1;
    if (y1 > ILI9341_HEIGHT - 1) y1 = ILI9341_HEIGHT - 1;
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (w <= 0 || h <= 0 || w > TILE_MAX || h > TILE_MAX) return;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t c = tortoise_image[(y0 + y) * ILI9341_WIDTH + x0 + x];
            for (int i = 0; i < MAX_OBJS; i++) {
                const obj_t *o = &objs[i];
                if (o->active) obj_pixel(o, x0 + x - (o->x >> FP), y0 + y - (o->y >> FP), &c);
            }
            tile[y * w + x] = c;
        }
    }
    ili9341_blit(x0, y0, w, h, tile);
}

static obj_t *spawn(int kind, int x, int y, int vx, int vy, int r, int life) {
    for (int i = 0; i < MAX_OBJS; i++) {
        obj_t *o = &objs[i];
        if (o->active) continue;
        *o = (obj_t){true, kind, x << FP, y << FP, vx, vy, 0, r, r, life, life};
        redraw(x - r - 1, y - r - 1, x + r + 1, y + r + 1);
        return o;
    }
    return NULL;
}

static void fire(void) {
    // Barrel points about 10 degrees above horizontal; vary speed and elevation.
    int speed = rnd_range(3 * ONE, 6 * ONE);
    int vx = speed;
    int vy = -rnd_range(ONE / 4, 3 * ONE);
    spawn(FLASH, MUZZLE_X + 3, MUZZLE_Y - 1, 0, 0, 9, 5);
    obj_t *ball = spawn(BALL, MUZZLE_X + 2, MUZZLE_Y, vx, vy, 5, 0);
    if (ball) ball->g = GRAVITY;
}

// Straight-ish shot in direction (dx, dy), each -1, 0 or 1, with little gravity.
static void fire_dir(int dx, int dy) {
    spawn(FLASH, MUZZLE_X + 3, MUZZLE_Y - 1, 0, 0, 9, 5);
    obj_t *ball = spawn(BALL, MUZZLE_X + 2, MUZZLE_Y, dx * 5 * ONE, dy * 5 * ONE, 5, 0);
    if (ball) ball->g = GRAVITY / 5;
}

static void update(obj_t *o) {
    int ox = o->x >> FP, oy = o->y >> FP, or_ = o->r;

    o->x += o->vx;
    o->y += o->vy;
    bool dead = false, hit_ground = false;

    if (o->kind == BALL) {
        o->vy += o->g;
        if ((o->y >> FP) >= GROUND_Y) {
            o->y = GROUND_Y << FP;
            dead = hit_ground = true;
        }
        if ((o->x >> FP) > ILI9341_WIDTH + o->r || (o->x >> FP) < -o->r ||
            (o->y >> FP) < -o->r)
            dead = true;
    } else {
        o->r = o->r0 * (o->life0 - ++o->life) / o->life0;
        if (o->life >= o->life0) dead = true;
    }

    int nx = o->x >> FP, ny = o->y >> FP;
    int r = (or_ > o->r ? or_ : o->r) + 1;
    int x0 = (ox < nx ? ox : nx) - r, x1 = (ox > nx ? ox : nx) + r;
    int y0 = (oy < ny ? oy : ny) - r, y1 = (oy > ny ? oy : ny) + r;

    if (dead) o->active = false;
    redraw(x0, y0, x1, y1);
    if (hit_ground) spawn(DUST, nx, GROUND_Y - 2, 0, -ONE / 8, 7, 8);
}

void tortoise_draw_scene(void) {
    ili9341_blit(0, 0, ILI9341_WIDTH, ILI9341_HEIGHT, tortoise_image);
}

void tortoise_buttons(uint8_t pressed) {
    if (pressed & JOY_UP) fire_dir(0, -1);
    if (pressed & JOY_DOWN) fire_dir(0, 1);
    if (pressed & JOY_LEFT) fire_dir(-1, 0);
    if (pressed & JOY_RIGHT) fire_dir(1, 0);
    if (pressed & JOY_MID) fire();
    if (pressed & JOY_SET) auto_fire = !auto_fire;
    if (pressed & JOY_RESET) {
        for (int i = 0; i < MAX_OBJS; i++) objs[i].active = false;
        tortoise_draw_scene();
    }
}

void tortoise_step(uint32_t ms) {
    static uint32_t last_ms, next_fire;
    if (ms - last_ms < FRAME_MS) return;
    last_ms = ms;

    if (!next_fire) next_fire = ms + 800;
    if ((int32_t)(ms - next_fire) >= 0) {
        if (auto_fire) fire();
        next_fire = ms + (uint32_t)rnd_range(500, 1500);
    }
    for (int i = 0; i < MAX_OBJS; i++)
        if (objs[i].active) update(&objs[i]);
}
