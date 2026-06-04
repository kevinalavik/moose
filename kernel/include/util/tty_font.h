#ifndef UTIL_TTY_FONT_H
#define UTIL_TTY_FONT_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint32_t codepoint;
    uint32_t width;
    uint32_t height;
    int32_t x_offset;
    int32_t y_offset;
    const uint8_t *bitmap;
} tty_glyph;

typedef struct
{
    uint32_t glyph_count;
    uint32_t width;
    uint32_t height;
    tty_glyph *glyphs;
    uint8_t *bitmap_pool;
    size_t bitmap_pool_size;
    size_t bitmap_pool_cap;
} tty_font;

#define TTY_STRIDE(w) (((uint32_t)(w) + 7u) / 8u)
#define TTY_GLYPH_ROW(g, r) ((g)->bitmap + (uint32_t)(r) * TTY_STRIDE((g)->width))

const tty_glyph *tty_glyph_for_codepoint(const tty_font *font, uint32_t cp);

#endif
