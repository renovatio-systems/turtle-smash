#ifndef TORTOISE_H
#define TORTOISE_H

#include <stdint.h>

// Draws the armoured tortoise picture (once, after the display is initialised).
void tortoise_draw_scene(void);

// Advances the cannon animation. `ms` is a free-running millisecond counter.
void tortoise_step(uint32_t ms);

// Handles newly pressed joystick buttons (JOY_* bits from joystick.h): direction
// buttons fire that way, MID fires the normal arc, SET toggles auto-fire and
// RESET clears the cannonballs.
void tortoise_buttons(uint8_t pressed);

#endif
