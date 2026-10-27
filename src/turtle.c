// The armoured tortoise: a body sprite that walks along the grass and a cannon
// barrel sprite that is rotated with the sin/cos table to the aimed angle.

#include "turtle.h"

#include <string.h>

#include "game_config.h"
#include "ili9488.h"
#include "input.h"
#include "physics.h"
#include "render.h"
#include "turtle_art.h"

#define METER_DX 10 // power meter, relative to the turtle's walk offset
#define METER_Y  62
#define METER_W  52
#define METER_H  8

typedef struct {
    int x;       // walk offset, px
    int step_y;  // walk wobble, 0 or 1 px down
    int angle;   // degrees
    int recoil;  // px
    bool charging;
    int charge;  // ticks
} state_t;

static state_t cur, shown; // shown = what is currently on screen
static int walk_ticks;
static int last_power = (POWER_MIN + POWER_MAX) / 2;
static bool guide_on;
typedef struct {
    int x, y;
} point_t;
static point_t guide[GUIDE_POINTS], guide_shown[GUIDE_POINTS];
static bool guide_visible;

static int power_for(int charge) {
    return POWER_MIN + (POWER_MAX - POWER_MIN) * charge / CHARGE_TICKS;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void pivot(const state_t *s, int *px, int *py) {
    *px = PIVOT_X + s->x;
    *py = PIVOT_Y + s->step_y;
}

static void muzzle(const state_t *s, int *mx, int *my) {
    int px, py;
    pivot(s, &px, &py);
    *mx = px + ((BARREL_LEN * fx_cos(s->angle) + (1 << 14)) >> FX_TRIG_SHIFT);
    *my = py - ((BARREL_LEN * fx_sin(s->angle) + (1 << 14)) >> FX_TRIG_SHIFT);
}

// Launch velocity for a power, along the barrel.
static void velocity(int angle, int power, int *vx, int *vy) {
    *vx = (power * fx_cos(angle)) >> FX_TRIG_SHIFT;
    *vy = -((power * fx_sin(angle)) >> FX_TRIG_SHIFT);
}

// Bounding box of the barrel sprite for a state (generous by a pixel).
static void barrel_box(const state_t *s, int *x0, int *y0, int *x1, int *y1) {
    int px, py;
    pivot(s, &px, &py);
    int c = fx_cos(s->angle), sn = fx_sin(s->angle);
    int us[2] = {BARREL_U0 - s->recoil - 1, BARREL_U0 + BARREL_W - s->recoil + 1};
    int vs[2] = {-BARREL_VH - 1, BARREL_VH + 1};
    *x0 = *y0 = 1 << 20;
    *x1 = *y1 = -(1 << 20);
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            int x = px + ((us[i] * c + vs[j] * sn) >> FX_TRIG_SHIFT);
            int y = py + ((-us[i] * sn + vs[j] * c) >> FX_TRIG_SHIFT);
            if (x < *x0) *x0 = x;
            if (x > *x1) *x1 = x;
            if (y < *y0) *y0 = y;
            if (y > *y1) *y1 = y;
        }
    }
    *x0 -= 2; *y0 -= 2; *x1 += 2; *y1 += 2;
}

static void mark_body(const state_t *s) {
    render_dirty(TURTLE_X0 + s->x, TURTLE_Y0 + s->step_y, TURTLE_W, TURTLE_H);
}

static void mark_barrel(const state_t *s) {
    int x0, y0, x1, y1;
    barrel_box(s, &x0, &y0, &x1, &y1);
    render_dirty(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

static void mark_meter(const state_t *s) {
    render_dirty(METER_DX + s->x, METER_Y, METER_W, METER_H);
}

// Dots of the predicted arc, using exactly the ball's integer physics.
static void predict(int power) {
    int x, y, vx, vy;
    muzzle(&cur, &x, &y);
    velocity(cur.angle, power, &vx, &vy);
    x <<= FP;
    y <<= FP;
    int n = 0;
    for (int tick = 1; n < GUIDE_POINTS; tick++) {
        x += vx;
        y += vy;
        vy += GRAVITY;
        if (tick % GUIDE_STEP == 0) {
            guide[n].x = x >> FP;
            guide[n].y = y >> FP;
            n++;
        }
    }
}

static void mark_guide(const point_t *g) {
    for (int i = 0; i < GUIDE_POINTS; i++) render_dirty(g[i].x - 1, g[i].y - 1, 4, 4);
}

void turtle_reset(void) {
    cur = (state_t){.x = WALK_MIN, .angle = ANGLE_START};
    walk_ticks = 0;
    last_power = (POWER_MIN + POWER_MAX) / 2;
    shown = cur;
    guide_visible = false;
    mark_body(&cur);
    mark_barrel(&cur);
}

void turtle_set_guide(bool on) {
    guide_on = on;
    if (guide_visible) mark_guide(guide_shown);
    guide_visible = false;
}

bool turtle_step(uint8_t held, uint8_t pressed, uint8_t released, bool can_fire, turtle_shot_t *shot) {
    bool fired = false;

    if (held & JOY_UP) cur.angle += AIM_STEP;
    if (held & JOY_DOWN) cur.angle -= AIM_STEP;
    cur.angle = clampi(cur.angle, ANGLE_MIN, ANGLE_MAX);

    int dir = ((held & JOY_RIGHT) ? 1 : 0) - ((held & JOY_LEFT) ? 1 : 0);
    if (dir) {
        if (++walk_ticks % WALK_PERIOD == 0) cur.x = clampi(cur.x + dir, WALK_MIN, WALK_MAX);
        cur.step_y = (walk_ticks / WOBBLE_TICKS) & 1;
    } else {
        walk_ticks = 0;
        cur.step_y = 0;
    }

    if (can_fire && (pressed & JOY_MID)) {
        cur.charging = true;
        cur.charge = 0;
    }
    if (cur.charging && (held & JOY_MID) && cur.charge < CHARGE_TICKS) cur.charge++;
    if (cur.charging && ((released & JOY_MID) || !(held & JOY_MID))) {
        cur.charging = false;
        if (can_fire) {
            int power = last_power = power_for(cur.charge);
            muzzle(&cur, &shot->x, &shot->y);
            velocity(cur.angle, power, &shot->vx, &shot->vy);
            cur.recoil = RECOIL_PX;
            fired = true;
        }
        cur.charge = 0;
    } else if (cur.recoil > 0) {
        cur.recoil--;
    }
    if (!can_fire) cur.charging = false;

    // Tell the renderer what changed.
    bool moved = cur.x != shown.x || cur.step_y != shown.step_y;
    if (moved) {
        mark_body(&shown);
        mark_body(&cur);
    }
    if (moved || cur.angle != shown.angle || cur.recoil != shown.recoil) {
        mark_barrel(&shown);
        mark_barrel(&cur);
    }
    if (moved || cur.charging != shown.charging || cur.charge != shown.charge) {
        mark_meter(&shown);
        mark_meter(&cur);
    }
    shown = cur;

    if (guide_on) {
        predict(cur.charging ? power_for(cur.charge) : last_power);
        if (guide_visible) mark_guide(guide_shown);
        mark_guide(guide);
        memcpy(guide_shown, guide, sizeof guide);
        guide_visible = true;
    }
    return fired;
}

void turtle_layer(const render_target_t *t) {
    int x0, y0, x1, y1;

    // Body.
    int bx = TURTLE_X0 + cur.x, by = TURTLE_Y0 + cur.step_y;
    if (render_clip(t, bx, by, TURTLE_W, TURTLE_H, &x0, &y0, &x1, &y1)) {
        for (int sy = y0; sy < y1; sy++) {
            uint16_t *p = render_px(t, x0, sy);
            const uint16_t *s = &turtle_body[sy - by][x0 - bx];
            for (int sx = x0; sx < x1; sx++, p++, s++)
                if (*s != SPRITE_KEY) *p = *s;
        }
    }

    // Barrel: for each screen pixel, find the barrel-sprite pixel that lands on it.
    int bx0, by0, bx1, by1, px, py;
    barrel_box(&cur, &bx0, &by0, &bx1, &by1);
    if (!render_clip(t, bx0, by0, bx1 - bx0 + 1, by1 - by0 + 1, &x0, &y0, &x1, &y1)) return;
    pivot(&cur, &px, &py);
    int c = fx_cos(cur.angle), sn = fx_sin(cur.angle);
    for (int sy = y0; sy < y1; sy++) {
        int dy = sy - py;
        uint16_t *p = render_px(t, x0, sy);
        for (int sx = x0; sx < x1; sx++, p++) {
            int dx = sx - px;
            int u = (dx * c - dy * sn + (1 << 14)) >> FX_TRIG_SHIFT;
            int v = (dx * sn + dy * c + (1 << 14)) >> FX_TRIG_SHIFT;
            int i = u + cur.recoil - BARREL_U0, j = v + BARREL_VH;
            if (i < 0 || i >= BARREL_W || j < 0 || j >= BARREL_H) continue;
            uint16_t col = turtle_barrel[j][i];
            if (col != SPRITE_KEY) *p = col;
        }
    }
}

void turtle_overlay_layer(const render_target_t *t) {
    if (cur.charging) {
        int x = METER_DX + cur.x;
        int fill = (METER_W - 4) * cur.charge / CHARGE_TICKS;
        uint16_t col = cur.charge * 100 < 50 * CHARGE_TICKS   ? RGB565(60, 220, 60)
                       : cur.charge * 100 < 85 * CHARGE_TICKS ? RGB565(255, 220, 0)
                                                              : RGB565(255, 60, 40);
        render_fill(t, x, METER_Y, METER_W, METER_H, RGB565(230, 230, 230));
        render_fill(t, x + 1, METER_Y + 1, METER_W - 2, METER_H - 2, RGB565(20, 20, 30));
        render_fill(t, x + 2, METER_Y + 2, fill, METER_H - 4, col);
    }
    if (guide_visible) {
        for (int i = 0; i < GUIDE_POINTS; i++)
            render_fill(t, guide_shown[i].x, guide_shown[i].y, 2, 2, RGB565(220, 230, 255));
    }
}
