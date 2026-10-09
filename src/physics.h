#ifndef PHYSICS_H
#define PHYSICS_H

#include <stdint.h>

// Fixed point used by the whole game loop (the RP2040 has no FPU): 8 fractional bits.
#define FP  8
#define ONE (1 << FP)

// Trig tables are Q15: the result is in -32768..32768, so (v * fx_cos(a)) >> FX_TRIG_SHIFT.
#define FX_TRIG_SHIFT 15

int fx_sin(int deg); // any integer angle in degrees
int fx_cos(int deg);

#endif
