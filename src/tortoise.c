// The armoured tortoise picture (src/tortoise_image.c, generated from
// assets/tortoise.png) with cannonballs fired from its cannon at a flying pig.
// Each player gets START_BALLS balls and scores a hit when a ball touches the
// pig; RESET starts a new game. The picture is a static background: whatever
// moves reports its old and new boxes to the dirty-rectangle renderer
// (render.c), which redraws just those areas from the picture plus the pig,
// the objects and the scoreboard layered on top. step() runs at 30 ticks/s.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ili9341.h"
#include "input.h"
#include "physics.h"
#include "render.h"
#include "tortoise.h"

extern const uint16_t tortoise_image[ILI9341_WIDTH * ILI9341_HEIGHT];

// Muzzle of the cannon and the grass line, in screen pixels.
#define MUZZLE_X  146
#define MUZZLE_Y  85
#define GROUND_Y  206

#define MAX_OBJS  32
#define GRAVITY   36 // 0.14 px/tick^2

#define START_BALLS 100
#define HUD_H       30 // scoreboard strip at the top of the screen
#define PIG_W       32
#define PIG_H       22
#define PIG_Y_MIN   (HUD_H + 6)
#define PIG_Y_MAX   140

enum { BALL, FLASH, DUST, PORK };

typedef struct {
    bool active;
    int kind;
    int x, y, vx, vy; // fixed point
    int g;            // gravity added to vy each frame
    int r, r0, life, life0;
} obj_t;

static obj_t objs[MAX_OBJS];
static bool auto_fire; // off by default so every ball is the player's

static int balls_left = START_BALLS, hits;
static uint32_t tick; // game ticks since start
static char hud_balls[24], hud_hits[24];

static struct {
    bool active;
    int x, y;  // top-left of the sprite, y includes the bobbing
    int fx, vx; // fixed-point x and speed (moves left)
    int base_y;
    int age;
    uint32_t respawn_at; // tick
} pig;
static uint16_t pig_px[2][PIG_H][PIG_W]; // two wing positions, 0 = transparent
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
    case PORK:
        *c = RGB565(255, 150, 180);
        return true;
    default:
        *c = RGB565(120, 100, 70);
        return true;
    }
}

// Draws the pig sprites once. The pig faces left and flaps its wings, with a
// dark outline so it reads clearly against the sky.
static void build_pig(void) {
    const uint16_t pink = RGB565(255, 150, 180), dark = RGB565(215, 100, 140),
                   snout = RGB565(255, 195, 205), wing = RGB565(235, 240, 255),
                   wing_shade = RGB565(165, 185, 235), outline = RGB565(120, 40, 70),
                   eye = RGB565(10, 10, 10);
    for (int f = 0; f < 2; f++) {
        uint16_t (*px)[PIG_W] = pig_px[f];
        for (int y = 0; y < PIG_H; y++) {
            for (int x = 0; x < PIG_W; x++) {
                uint16_t c = 0;
                int bx = x - 19, by = y - 13, hx = x - 8, hy = y - 13, sx = x - 2, sy = y - 14;
                if (bx * bx * 49 + by * by * 121 <= 121 * 49) c = pink;           // body
                if (hx * hx + hy * hy <= 49) c = pink;                            // head
                if (sx * sx * 9 + sy * sy * 9 <= 81) c = snout;                   // snout
                if (y >= 3 && y <= 7 && x >= 5 + (y - 3) / 2 && x <= 9 - (y - 3) / 2) c = dark; // ear
                if (y >= 19 && ((x >= 12 && x <= 14) || (x >= 24 && x <= 26))) c = dark;        // legs
                if (((x == 30 || x == 31) && y >= 9 && y <= 12) || (x == 29 && y == 9) ||
                    (x == 30 && y == 8) || (x == 31 && y == 8))
                    c = dark; // curly tail
                if (f == 0) { // wing up: tall triangle, shaded lower half
                    int hw = y * 7 / 10;
                    if (y <= 10 && x >= 19 - hw && x <= 19 + hw) c = y < 5 ? wing : wing_shade;
                } else if (y >= 6 && y <= 10 && x >= 13 && x <= 27) { // wing down: flat
                    c = y < 8 ? wing : wing_shade;
                }
                px[y][x] = c;
            }
        }
        for (int y = 12; y < 14; y++) px[y][2] = dark;                   // nostrils
        px[14][2] = dark;
        px[11][6] = eye; px[11][7] = eye; px[12][6] = eye; px[12][7] = eye;
        px[11][6] = COLOR_WHITE;

        // Outline pass: transparent pixels next to the sprite become dark.
        static uint16_t out[PIG_H][PIG_W];
        for (int y = 0; y < PIG_H; y++) {
            for (int x = 0; x < PIG_W; x++) {
                out[y][x] = px[y][x];
                if (px[y][x]) continue;
                if ((x > 0 && px[y][x - 1]) || (x < PIG_W - 1 && px[y][x + 1]) ||
                    (y > 0 && px[y - 1][x]) || (y < PIG_H - 1 && px[y + 1][x]))
                    out[y][x] = outline;
            }
        }
        for (int y = 0; y < PIG_H; y++)
            for (int x = 0; x < PIG_W; x++) px[y][x] = out[y][x];
    }
}

// Scoreboard text is rebuilt when a number changes; the layer below draws it.
static void hud_draw(void) {
    snprintf(hud_balls, sizeof hud_balls, "BALLS %3d", balls_left);
    snprintf(hud_hits, sizeof hud_hits, "HITS %3d", hits);
    render_dirty(0, 0, ILI9341_WIDTH, HUD_H);
}

static void hud_layer(const render_target_t *t) {
    if (t->y >= HUD_H) return;
    render_text(t, 4, 2, hud_balls, balls_left ? COLOR_YELLOW : COLOR_RED, COLOR_BLACK, 2);
    render_text(t, ILI9341_WIDTH - 8 * 12 - 4, 2, hud_hits, COLOR_GREEN, COLOR_BLACK, 2);
    if (!balls_left) {
        const char *msg = "  OUT OF BALLS - PRESS RESET  ";
        render_text(t, (ILI9341_WIDTH - (int)strlen(msg) * 6) / 2, 20, msg, COLOR_GREY, COLOR_BLACK, 1);
    }
}

static void pig_layer(const render_target_t *t) {
    int x0, y0, x1, y1;
    if (!pig.active || !render_clip(t, pig.x, pig.y, PIG_W, PIG_H, &x0, &y0, &x1, &y1)) return;
    const uint16_t(*spr)[PIG_W] = pig_px[(pig.age / 3) & 1];
    for (int sy = y0; sy < y1; sy++) {
        uint16_t *p = render_px(t, x0, sy);
        for (int sx = x0; sx < x1; sx++, p++) {
            uint16_t c = spr[sy - pig.y][sx - pig.x];
            if (c) *p = c;
        }
    }
}

static void objs_layer(const render_target_t *t) {
    for (int i = 0; i < MAX_OBJS; i++) {
        const obj_t *o = &objs[i];
        if (!o->active) continue;
        int cx = o->x >> FP, cy = o->y >> FP, x0, y0, x1, y1;
        if (!render_clip(t, cx - o->r, cy - o->r, 2 * o->r + 1, 2 * o->r + 1, &x0, &y0, &x1, &y1))
            continue;
        for (int sy = y0; sy < y1; sy++) {
            for (int sx = x0; sx < x1; sx++) {
                uint16_t c;
                if (obj_pixel(o, sx - cx, sy - cy, &c)) *render_px(t, sx, sy) = c;
            }
        }
    }
}

static void mark_obj(const obj_t *o, int r) {
    render_dirty((o->x >> FP) - r - 1, (o->y >> FP) - r - 1, 2 * r + 3, 2 * r + 3);
}

static obj_t *spawn(int kind, int x, int y, int vx, int vy, int r, int life) {
    for (int i = 0; i < MAX_OBJS; i++) {
        obj_t *o = &objs[i];
        if (o->active) continue;
        *o = (obj_t){.active = true, .kind = kind, .x = x << FP, .y = y << FP, .vx = vx, .vy = vy,
                     .r = r, .r0 = r, .life = 0, .life0 = life};
        mark_obj(o, r);
        return o;
    }
    return NULL;
}

// Fires one ball from the muzzle if any are left.
static void launch(int vx, int vy, int g) {
    if (balls_left <= 0) return;
    spawn(FLASH, MUZZLE_X + 3, MUZZLE_Y - 1, 0, 0, 9, 7);
    obj_t *ball = spawn(BALL, MUZZLE_X + 2, MUZZLE_Y, vx, vy, 5, 0);
    if (!ball) return;
    ball->g = g;
    balls_left--;
    hud_draw();
}

static void fire(void) {
    // Barrel points about 10 degrees above horizontal; vary speed and elevation.
    int speed = rnd_range(4 * ONE, 8 * ONE);
    int vx = speed;
    int vy = -rnd_range(ONE / 3, 4 * ONE);
    launch(vx, vy, GRAVITY);
}

// Straight-ish shot in direction (dx, dy), each -1, 0 or 1, with little gravity.
static void fire_dir(int dx, int dy) {
    launch(dx * 7 * ONE, dy * 7 * ONE, GRAVITY / 5);
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
    render_dirty(ox - r, oy - r, 2 * r + 1, 2 * r + 1); // where it was
    render_dirty(nx - r, ny - r, 2 * r + 1, 2 * r + 1); // where it is now

    if (dead) o->active = false;
    if (hit_ground) spawn(DUST, nx, GROUND_Y - 2, 0, -ONE / 8, 7, 11);
}

static void pig_launch(void) {
    pig.active = true;
    pig.fx = ILI9341_WIDTH << FP;
    pig.vx = rnd_range(ONE * 4 / 3, 3 * ONE);
    pig.base_y = rnd_range(PIG_Y_MIN + 2, PIG_Y_MAX);
    pig.age = 0;
    pig.x = pig.fx >> FP;
    pig.y = pig.base_y;
    render_dirty(pig.x, pig.y, PIG_W, PIG_H);
}

static void pig_move(void) {
    if (!pig.active) {
        if ((int32_t)(tick - pig.respawn_at) >= 0) pig_launch();
        return;
    }
    render_dirty(pig.x, pig.y, PIG_W, PIG_H); // where it was
    pig.fx -= pig.vx;
    pig.age++;
    pig.x = pig.fx >> FP;
    pig.y = pig.base_y + ((fx_sin(pig.age * 15) * 2 + (1 << (FX_TRIG_SHIFT - 1))) >> FX_TRIG_SHIFT);
    if (pig.x < -PIG_W) {
        pig.active = false;
        pig.respawn_at = tick + (uint32_t)rnd_range(15, 45);
    }
    render_dirty(pig.x, pig.y, PIG_W, PIG_H);
}

// A ball touching the pig scores a hit: the pig bursts and comes back later.
static void check_hits(void) {
    if (!pig.active) return;
    for (int i = 0; i < MAX_OBJS; i++) {
        obj_t *b = &objs[i];
        if (!b->active || b->kind != BALL) continue;
        int bx = b->x >> FP, by = b->y >> FP, m = b->r - 1;
        if (bx < pig.x - m || bx > pig.x + PIG_W + m || by < pig.y - m || by > pig.y + PIG_H + m) continue;

        int cx = pig.x + PIG_W / 2, cy = pig.y + PIG_H / 2;
        mark_obj(b, b->r);
        render_dirty(pig.x, pig.y, PIG_W, PIG_H);
        b->active = false;
        pig.active = false;
        pig.respawn_at = tick + 45;
        hits++;
        spawn(FLASH, cx, cy, 0, 0, 12, 8);
        for (int k = 0; k < 5; k++)
            spawn(PORK, cx, cy, rnd_range(-ONE * 4 / 3, ONE * 4 / 3), rnd_range(-ONE * 4 / 3, ONE * 4 / 3), 4, 13);
        hud_draw();
        return;
    }
}

void tortoise_draw_scene(void) {
    static bool built;
    if (!built) {
        build_pig();
        render_init(tortoise_image);
        render_add_layer(pig_layer);
        render_add_layer(objs_layer);
        render_add_layer(hud_layer);
        built = true;
    }
    hud_draw();
    render_dirty_all();
    render_flush();
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
        balls_left = START_BALLS;
        hits = 0;
        pig.active = false;
        pig.respawn_at = tick; // pig returns on the next tick
        hud_draw();
        render_dirty_all();
    }
}

void tortoise_step(void) {
    static uint32_t next_fire;
    tick++;

    if (!next_fire) next_fire = tick + 24;
    if ((int32_t)(tick - next_fire) >= 0) {
        if (auto_fire) fire();
        next_fire = tick + (uint32_t)rnd_range(15, 45);
    }
    for (int i = 0; i < MAX_OBJS; i++)
        if (objs[i].active) update(&objs[i]);
    pig_move();
    check_hits();
}
