// Seven active-low buttons on GPIOs with internal pull-ups, debounced by
// requiring the new level to stay put for INPUT_DEBOUNCE_MS.

#include <stdbool.h>

#include "config.h"
#include "input.h"
#include "pico/stdlib.h"

#define INPUT_DEBOUNCE_MS 15

static const struct {
    uint pin;
    uint8_t bit;
} buttons[] = {
    {PIN_JOY_UP, JOY_UP},       {PIN_JOY_DOWN, JOY_DOWN}, {PIN_JOY_LEFT, JOY_LEFT},
    {PIN_JOY_RIGHT, JOY_RIGHT}, {PIN_JOY_MID, JOY_MID},   {PIN_JOY_SET, JOY_SET},
    {PIN_JOY_RESET, JOY_RESET},
};
#define N_BUTTONS (sizeof buttons / sizeof buttons[0])

static bool changing[N_BUTTONS];   // raw level differs from the debounced one
static uint32_t since[N_BUTTONS];  // when it started to differ
static uint8_t held, pressed, released;

void input_init(void) {
    for (unsigned i = 0; i < N_BUTTONS; i++) {
        gpio_init(buttons[i].pin);
        gpio_set_dir(buttons[i].pin, GPIO_IN);
        gpio_pull_up(buttons[i].pin);
    }
}

void input_poll(uint32_t now_ms) {
    for (unsigned i = 0; i < N_BUTTONS; i++) {
        uint8_t bit = buttons[i].bit;
        bool raw = !gpio_get(buttons[i].pin), stable = held & bit;
        if (raw == stable) {
            changing[i] = false;
        } else if (!changing[i]) {
            changing[i] = true;
            since[i] = now_ms;
        } else if (now_ms - since[i] >= INPUT_DEBOUNCE_MS) {
            changing[i] = false;
            if (raw) {
                held |= bit;
                pressed |= bit;
            } else {
                held &= (uint8_t)~bit;
                released |= bit;
            }
        }
    }
}

uint8_t input_held(void) { return held; }

uint8_t input_pressed(void) {
    uint8_t p = pressed;
    pressed = 0;
    return p;
}

uint8_t input_released(void) {
    uint8_t r = released;
    released = 0;
    return r;
}
