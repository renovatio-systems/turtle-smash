#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H

// Gameplay constants. Speeds are in pixels per 1/30 s tick, in Q8 fixed point (ONE = 1 px).
#include "physics.h"

#define GRAVITY      36 // added to a ball's vy every tick (0.14 px/tick^2)
#define START_BALLS  100

// Cannon
#define ANGLE_MIN    10 // degrees above horizontal
#define ANGLE_MAX    65
#define ANGLE_START  25
#define AIM_STEP     1  // degrees per tick while UP/DOWN is held
#define POWER_MIN    (3 * ONE)
#define POWER_MAX    (8 * ONE)
#define CHARGE_TICKS 40 // holding CENTER this long gives full power
#define RECOIL_PX    5  // barrel kick on firing, recovers 1 px per tick

// Pig
#define PIG_VX_MIN   (ONE)         // was 4*ONE/3, too fast to track
#define PIG_VX_MAX   (2 * ONE)     // was 3*ONE

// Walking
#define WALK_MIN     0  // turtle offset from its starting spot, px
#define WALK_MAX     120
#define WALK_PERIOD  2  // ticks per pixel
#define WOBBLE_TICKS 6  // ticks per walk-wobble frame

// Aim guide (first round only)
#define GUIDE_POINTS 8
#define GUIDE_STEP   3  // ticks of flight between dots

#endif
