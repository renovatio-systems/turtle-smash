#ifndef TORTOISE_H
#define TORTOISE_H

#include <stdint.h>

// Draws the armoured tortoise picture (once, after the display is initialised).
void tortoise_draw_scene(void);

// Advances the cannon animation. `ms` is a free-running millisecond counter.
void tortoise_step(uint32_t ms);

#endif
