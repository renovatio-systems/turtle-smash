// Host stub of the Pico SDK subset used by the firmware. Implemented in emulator.c.
#ifndef PICO_STDLIB_H
#define PICO_STDLIB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef unsigned int uint;

#define GPIO_OUT           1
#define GPIO_FUNC_SPI      1
#define PICO_ERROR_TIMEOUT (-1)

void gpio_init(uint pin);
void gpio_set_dir(uint pin, bool out);
void gpio_put(uint pin, bool value);
void gpio_set_function(uint pin, int fn);

void sleep_ms(uint32_t ms);
void sleep_us(uint64_t us);

bool stdio_init_all(void);
int getchar_timeout_us(uint32_t timeout_us);

#endif
