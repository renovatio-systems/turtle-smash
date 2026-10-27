#ifndef FONT5X7_H
#define FONT5X7_H

#include <stdint.h>

// Classic 5x7 bitmap font. Returns 5 column bytes, bit 0 = top row.
// Characters without a glyph render as a hollow box.
const uint8_t *font5x7_glyph(char c);

#endif
