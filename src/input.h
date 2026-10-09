#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

enum {
    JOY_UP = 1 << 0,
    JOY_DOWN = 1 << 1,
    JOY_LEFT = 1 << 2,
    JOY_RIGHT = 1 << 3,
    JOY_MID = 1 << 4,
    JOY_SET = 1 << 5,
    JOY_RESET = 1 << 6,
};

void input_init(void);

// Samples the switches; call as often as convenient. A level change only counts
// once it has been stable for INPUT_DEBOUNCE_MS. `now_ms` is a free-running ms counter.
void input_poll(uint32_t now_ms);

// Debounced buttons currently held down.
uint8_t input_held(void);

// Buttons pressed / released since the last call (cleared by reading), so no
// edge is lost if the game tick runs less often than input_poll().
uint8_t input_pressed(void);
uint8_t input_released(void);

#endif
