/*
 * font5x7.h
 * ---------------------------------------------------------------------------
 * Minimal 5x7 dot-matrix font: 5 bytes per glyph (one byte per column),
 * bit0 = top pixel row .. bit6 = bottom pixel row, bit7 unused. This is the
 * same byte convention and the same glyph shapes as the long-standard
 * "5x7 ASCII font" used across countless open embedded/Arduino SSD1306
 * projects (e.g. Adafruit's GFX default font) -- reusing the well-verified
 * standard shapes here rather than hand-drawing new ones, since a font
 * table is exactly the kind of thing you want to be boringly correct.
 *
 * Only the subset of characters this project's two OLED strings actually
 * use is included, to keep this small:
 *   space 0-9 A B C D E F G H I K L M O P R T U : % - .
 *
 * If you add new display text later (e.g. a lowercase unit, or additional
 * letters), extend font5x7_lookup() below with more cases using the same
 * standard 5x7 glyph values -- don't hand-derive new bit patterns from
 * scratch; copy them from the same reference table this one is drawn from.
 */

#ifndef FONT5X7_H
#define FONT5X7_H

#include <stdint.h>

static inline const uint8_t *font5x7_lookup(char c)
{
    switch (c) {
        case ' ': { static const uint8_t g[5] = {0x00,0x00,0x00,0x00,0x00}; return g; }
        case '0': { static const uint8_t g[5] = {0x3E,0x51,0x49,0x45,0x3E}; return g; }
        case '1': { static const uint8_t g[5] = {0x00,0x42,0x7F,0x40,0x00}; return g; }
        case '2': { static const uint8_t g[5] = {0x42,0x61,0x51,0x49,0x46}; return g; }
        case '3': { static const uint8_t g[5] = {0x21,0x41,0x45,0x4B,0x31}; return g; }
        case '4': { static const uint8_t g[5] = {0x18,0x14,0x12,0x7F,0x10}; return g; }
        case '5': { static const uint8_t g[5] = {0x27,0x45,0x45,0x45,0x39}; return g; }
        case '6': { static const uint8_t g[5] = {0x3C,0x4A,0x49,0x49,0x30}; return g; }
        case '7': { static const uint8_t g[5] = {0x01,0x71,0x09,0x05,0x03}; return g; }
        case '8': { static const uint8_t g[5] = {0x36,0x49,0x49,0x49,0x36}; return g; }
        case '9': { static const uint8_t g[5] = {0x06,0x49,0x49,0x29,0x1E}; return g; }
        case 'A': { static const uint8_t g[5] = {0x7E,0x11,0x11,0x11,0x7E}; return g; }
        case 'B': { static const uint8_t g[5] = {0x7F,0x49,0x49,0x49,0x36}; return g; }
        case 'C': { static const uint8_t g[5] = {0x3E,0x41,0x41,0x41,0x22}; return g; }
        case 'D': { static const uint8_t g[5] = {0x7F,0x41,0x41,0x22,0x1C}; return g; }
        case 'E': { static const uint8_t g[5] = {0x7F,0x49,0x49,0x49,0x41}; return g; }
        case 'F': { static const uint8_t g[5] = {0x7F,0x09,0x09,0x09,0x01}; return g; }
        case 'G': { static const uint8_t g[5] = {0x3E,0x41,0x49,0x49,0x7A}; return g; }
        case 'H': { static const uint8_t g[5] = {0x7F,0x08,0x08,0x08,0x7F}; return g; }
        case 'I': { static const uint8_t g[5] = {0x00,0x41,0x7F,0x41,0x00}; return g; }
        case 'K': { static const uint8_t g[5] = {0x7F,0x08,0x14,0x22,0x41}; return g; }
        case 'L': { static const uint8_t g[5] = {0x7F,0x40,0x40,0x40,0x40}; return g; }
        case 'M': { static const uint8_t g[5] = {0x7F,0x02,0x0C,0x02,0x7F}; return g; }
        case 'O': { static const uint8_t g[5] = {0x3E,0x41,0x41,0x41,0x3E}; return g; }
        case 'P': { static const uint8_t g[5] = {0x7F,0x09,0x09,0x09,0x06}; return g; }
        case 'R': { static const uint8_t g[5] = {0x7F,0x09,0x19,0x29,0x46}; return g; }
        case 'T': { static const uint8_t g[5] = {0x01,0x01,0x7F,0x01,0x01}; return g; }
        case 'U': { static const uint8_t g[5] = {0x3F,0x40,0x40,0x40,0x3F}; return g; }
        case ':': { static const uint8_t g[5] = {0x00,0x36,0x36,0x00,0x00}; return g; }
        case '%': { static const uint8_t g[5] = {0x23,0x13,0x08,0x64,0x62}; return g; }
        case '-': { static const uint8_t g[5] = {0x08,0x08,0x08,0x08,0x08}; return g; }
        case '.': { static const uint8_t g[5] = {0x00,0x60,0x60,0x00,0x00}; return g; }
        default:  return 0; /* unsupported character -- caller draws blank */
    }
}

#endif /* FONT5X7_H */
