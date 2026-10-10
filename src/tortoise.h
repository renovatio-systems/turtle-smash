#ifndef TORTOISE_H
#define TORTOISE_H

#include <stdint.h>

// Registers the renderer layers and paints the whole scene (call once after the
// display is initialised).
void tortoise_draw_scene(void);

// Advances the game by one fixed 1/30 s tick. `held`, `pressed` and `released` are
// JOY_* masks from input.h (see turtle_step for the controls; RESET starts a new
// game). The caller then runs render_flush().
void tortoise_step(uint8_t held, uint8_t pressed, uint8_t released);

#endif
