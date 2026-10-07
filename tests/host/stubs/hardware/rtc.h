// Host stub of hardware/rtc.h. Implemented in emulator.c.
#ifndef HARDWARE_RTC_H
#define HARDWARE_RTC_H

#include "pico/stdlib.h"

typedef struct {
    int16_t year;
    int8_t month;
    int8_t day;
    int8_t dotw;
    int8_t hour;
    int8_t min;
    int8_t sec;
} datetime_t;

void rtc_init(void);
bool rtc_set_datetime(const datetime_t *t);
bool rtc_get_datetime(datetime_t *t);

#endif
