// The armoured tortoise picture (src/tortoise_image.c, generated from
// assets/tortoise.png) with cannonballs fired from its cannon at a flying pig.
// Each player gets START_BALLS balls and scores a hit when a ball touches the
// pig; RESET starts a new game. The picture is drawn once; each frame only
// small tiles around moving objects are redrawn, composed from the picture plus
// the pig and the objects on top of it.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static uint32_t last_ms; // time of the last animation frame

static struct {
    bool active;
    int x, y;  // top-left of the sprite, y includes the bobbing
    int fx, vx; // fixed-point x and speed (moves left)
    int base_y;
    int age;
    uint32_t respawn_at;
} pig;
static uint16_t pig_px[2][PIG_H][PIG_W]; // two wing positions, 0 = transparent
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

static void hud_draw(void) {
    char buf[16];
    snprintf(buf, sizeof buf, "BALLS %3d", balls_left);
    ili9341_draw_string(4, 2, buf, balls_left ? COLOR_YELLOW : COLOR_RED, COLOR_BLACK, 2);
    snprintf(buf, sizeof buf, "HITS %3d", hits);
    ili9341_draw_string(ILI9341_WIDTH - 8 * 12 - 4, 2, buf, COLOR_GREEN, COLOR_BLACK, 2);
    const char *msg = balls_left ? "" : "  OUT OF BALLS - PRESS RESET  ";
    int n = balls_left ? 30 : (int)strlen(msg);
    char line[32];
    snprintf(line, sizeof line, "%*s", n, msg);
    ili9341_draw_string((ILI9341_WIDTH - n * 6) / 2, 20, line, COLOR_GREY, COLOR_BLACK, 1);
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
            if (pig.active) {
                int px = x0 + x - pig.x, py = y0 + y - pig.y;
                if (px >= 0 && px < PIG_W && py >= 0 && py < PIG_H && pig_px[(pig.age / 4) & 1][py][px])
                    c = pig_px[(pig.age / 4) & 1][py][px];
            }
            for (int i = 0; i < MAX_OBJS; i++) {
                const obj_t *o = &objs[i];
                if (o->active) obj_pixel(o, x0 + x - (o->x >> FP), y0 + y - (o->y >> FP), &c);
            }
            tile[y * w + x] = c;
        }
    }
    ili9341_blit(x0, y0, w, h, tile);
    if (y0 < HUD_H) hud_draw(); // a ball passing through the scoreboard
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

// Fires one ball from the muzzle if any are left.
static void launch(int vx, int vy, int g) {
    if (balls_left <= 0) return;
    spawn(FLASH, MUZZLE_X + 3, MUZZLE_Y - 1, 0, 0, 9, 5);
    obj_t *ball = spawn(BALL, MUZZLE_X + 2, MUZZLE_Y, vx, vy, 5, 0);
    if (!ball) return;
    ball->g = g;
    balls_left--;
    hud_draw();
}

static void fire(void) {
    // Barrel points about 10 degrees above horizontal; vary speed and elevation.
    int speed = rnd_range(3 * ONE, 6 * ONE);
    int vx = speed;
    int vy = -rnd_range(ONE / 4, 3 * ONE);
    launch(vx, vy, GRAVITY);
}

// Straight-ish shot in direction (dx, dy), each -1, 0 or 1, with little gravity.
static void fire_dir(int dx, int dy) {
    launch(dx * 5 * ONE, dy * 5 * ONE, GRAVITY / 5);
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

static void redraw_pig(int ox, int oy) {
    int x0 = (ox < pig.x ? ox : pig.x) - 1, x1 = (ox > pig.x ? ox : pig.x) + PIG_W;
    int y0 = (oy < pig.y ? oy : pig.y) - 1, y1 = (oy > pig.y ? oy : pig.y) + PIG_H;
    redraw(x0, y0, x1, y1);
}

static void pig_launch(void) {
    static const int bob[8] = {0, 1, 2, 2, 1, 0, -1, -1};
    pig.active = true;
    pig.fx = ILI9341_WIDTH << FP;
    pig.vx = rnd_range(ONE, 2 * ONE + ONE / 4);
    pig.base_y = rnd_range(PIG_Y_MIN + 2, PIG_Y_MAX);
    pig.age = 0;
    pig.x = pig.fx >> FP;
    pig.y = pig.base_y + bob[0];
    redraw(pig.x, pig.y, pig.x + PIG_W - 1, pig.y + PIG_H - 1);
}

static void pig_move(uint32_t ms) {
    static const int bob[8] = {0, 1, 2, 2, 1, 0, -1, -1};
    if (!pig.active) {
        if ((int32_t)(ms - pig.respawn_at) >= 0) pig_launch();
        return;
    }
    int ox = pig.x, oy = pig.y;
    pig.fx -= pig.vx;
    pig.age++;
    pig.x = pig.fx >> FP;
    pig.y = pig.base_y + bob[(pig.age / 3) & 7];
    if (pig.x < -PIG_W) {
        pig.active = false;
        pig.respawn_at = ms + (uint32_t)rnd_range(500, 1500);
    }
    redraw_pig(ox, oy);
}

// A ball touching the pig scores a hit: the pig bursts and comes back later.
static void check_hits(uint32_t ms) {
    if (!pig.active) return;
    for (int i = 0; i < MAX_OBJS; i++) {
        obj_t *b = &objs[i];
        if (!b->active || b->kind != BALL) continue;
        int bx = b->x >> FP, by = b->y >> FP, m = b->r - 1;
        if (bx < pig.x - m || bx > pig.x + PIG_W + m || by < pig.y - m || by > pig.y + PIG_H + m) continue;

        int cx = pig.x + PIG_W / 2, cy = pig.y + PIG_H / 2;
        b->active = false;
        pig.active = false;
        pig.respawn_at = ms + 1500;
        hits++;
        redraw(bx - b->r - 1, by - b->r - 1, bx + b->r + 1, by + b->r + 1);
        redraw(pig.x - 1, pig.y - 1, pig.x + PIG_W, pig.y + PIG_H);
        spawn(FLASH, cx, cy, 0, 0, 12, 6);
        for (int k = 0; k < 5; k++)
            spawn(PORK, cx, cy, rnd_range(-ONE, ONE), rnd_range(-ONE, ONE), 4, 10);
        hud_draw();
        return;
    }
}

void tortoise_draw_scene(void) {
    static bool built;
    if (!built) {
        build_pig();
        built = true;
    }
    ili9341_blit(0, 0, ILI9341_WIDTH, ILI9341_HEIGHT, tortoise_image);
    hud_draw();
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
        pig.respawn_at = last_ms; // pig returns on the next frame
        tortoise_draw_scene();
    }
}

void tortoise_step(uint32_t ms) {
    static uint32_t next_fire;
    if (ms - last_ms < FRAME_MS) return;
    last_ms = ms;

    if (!next_fire) next_fire = ms + 800;
    if ((int32_t)(ms - next_fire) >= 0) {
        if (auto_fire) fire();
        next_fire = ms + (uint32_t)rnd_range(500, 1500);
    }
    for (int i = 0; i < MAX_OBJS; i++)
        if (objs[i].active) update(&objs[i]);
    pig_move(ms);
    check_hits(ms);
}
