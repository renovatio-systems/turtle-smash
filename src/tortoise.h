#ifndef TORTOISE_H
#define TORTOISE_H

#include <stdint.h>

// Registers the renderer layers and paints the whole scene (call once after the
// display is initialised).
void tortoise_draw_scene(void);

// Advances the game by one fixed 1/30 s tick. The caller then runs render_flush().
void tortoise_step(void);

// Handles newly pressed buttons (JOY_* bits from input.h): direction buttons
// fire that way, MID fires the normal arc, SET toggles auto-fire and RESET
// starts a new game. Call before tortoise_step().
void tortoise_buttons(uint8_t pressed);

#endif
