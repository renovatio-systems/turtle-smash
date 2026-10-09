#ifndef DRAGON_H
#define DRAGON_H

#include <stdint.h>

// Draws the static dragon scene (once, after the display is initialised).
void dragon_draw_scene(void);

// Advances the fire-breath animation one frame. `ms` is a free-running
// millisecond counter; the dragon breathes in a 4 s cycle.
void dragon_step(uint32_t ms);

#endif
