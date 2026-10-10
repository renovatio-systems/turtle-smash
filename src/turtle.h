#ifndef TURTLE_CONTROL_H
#define TURTLE_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "render.h"

typedef struct {
    int x, y;   // muzzle, screen pixels
    int vx, vy; // Q8 pixels per tick
} turtle_shot_t;

// Back to the starting spot and angle; idle. Marks the turtle dirty.
void turtle_reset(void);

// Shows a dotted preview of the shot's arc (the first-round tutorial).
void turtle_set_guide(bool on);

// One tick of control. Buttons are JOY_* masks: UP/DOWN aim, LEFT/RIGHT walk, and
// CENTER charges while held and fires on release. Returns true and fills `shot`
// when a ball should be launched; `can_fire` false suppresses charging and firing.
bool turtle_step(uint8_t held, uint8_t pressed, uint8_t released, bool can_fire, turtle_shot_t *shot);

// Layers for the renderer: the turtle itself, then the power meter and aim guide.
void turtle_layer(const render_target_t *t);
void turtle_overlay_layer(const render_target_t *t);

#endif
