#ifndef JOYSTICK_H
#define JOYSTICK_H

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

void joystick_init(void);

// Call every few ms. Returns the buttons newly pressed (debounced) since the last call.
uint8_t joystick_poll(void);

#endif
