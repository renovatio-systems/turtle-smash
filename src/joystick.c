// Seven active-low buttons on GPIOs with internal pull-ups, debounced by
// requiring the new level on DEBOUNCE_POLLS consecutive polls.

#include <stdbool.h>

#include "config.h"
#include "joystick.h"
#include "pico/stdlib.h"

#define DEBOUNCE_POLLS 4

static const struct {
    uint pin;
    uint8_t bit;
} buttons[] = {
    {PIN_JOY_UP, JOY_UP},       {PIN_JOY_DOWN, JOY_DOWN}, {PIN_JOY_LEFT, JOY_LEFT},
    {PIN_JOY_RIGHT, JOY_RIGHT}, {PIN_JOY_MID, JOY_MID},   {PIN_JOY_SET, JOY_SET},
    {PIN_JOY_RESET, JOY_RESET},
};
#define N_BUTTONS (sizeof buttons / sizeof buttons[0])

static bool down[N_BUTTONS];
static uint8_t count[N_BUTTONS];

void joystick_init(void) {
    for (unsigned i = 0; i < N_BUTTONS; i++) {
        gpio_init(buttons[i].pin);
        gpio_set_dir(buttons[i].pin, GPIO_IN);
        gpio_pull_up(buttons[i].pin);
    }
}

uint8_t joystick_poll(void) {
    uint8_t pressed = 0;
    for (unsigned i = 0; i < N_BUTTONS; i++) {
        bool raw = !gpio_get(buttons[i].pin);
        if (raw == down[i]) {
            count[i] = 0;
        } else if (++count[i] >= DEBOUNCE_POLLS) {
            down[i] = raw;
            count[i] = 0;
            if (raw) pressed |= buttons[i].bit;
        }
    }
    return pressed;
}
