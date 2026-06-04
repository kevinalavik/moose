#ifndef UTIL_PSF_H
#define UTIL_PSF_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <util/tty_font.h>

#define PSF1_MAGIC0 0x36
#define PSF1_MAGIC1 0x04
#define PSF1_MODE512 0x01
#define PSF1_MODEHASTAB 0x02
#define PSF1_MODEHASSEQ 0x04

#define PSF2_MAGIC0 0x72
#define PSF2_MAGIC1 0xb5
#define PSF2_MAGIC2 0x4a
#define PSF2_MAGIC3 0x86
#define PSF2_HAS_UNICODE_TABLE 0x01

typedef struct
{
    uint32_t version;
    uint32_t glyph_count;
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_glyph;
    const uint8_t *glyph_data;
    const uint8_t *unicode_data;
    size_t unicode_bytes;
    const uint8_t *raw;
    size_t raw_size;
} psf_font;

typedef enum
{
    PSF_OK = 0,
    PSF_ERR_NULL = -1,
    PSF_ERR_MAGIC = -2,
    PSF_ERR_VERSION = -3,
    PSF_ERR_SIZE = -4,
    PSF_ERR_OVERFLOW = -5,
    PSF_ERR_POOL = -6,
} psf_result;

psf_result psf_parse(const void *data, size_t size, psf_font *font);
psf_result psf_to_tty(psf_font *font, tty_font *tty,
                      tty_glyph *glyphs, uint32_t glyph_cap,
                      uint8_t *pool, size_t pool_cap);
const char *psf_result_str(psf_result r);

#ifdef PSF_IMPLEMENTATION

static uint32_t psf__r32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void psf__memset(void *d, int c, size_t n)
{
    unsigned char *p = d;
    while (n--)
        *p++ = (unsigned char)c;
}

static uint32_t psf__glyph_codepoint(const psf_font *font, uint32_t idx)
{
    if (!font->unicode_data || font->unicode_bytes == 0)
        return idx;

    if (font->version == 1)
    {
        const uint8_t *p = font->unicode_data;
        const uint8_t *end = p + font->unicode_bytes;
        uint32_t gi = 0;
        bool has_seq = (font->raw[2] & PSF1_MODEHASSEQ) != 0;

        while (p + 2 <= end && gi <= idx)
        {
            uint16_t val = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
            if (has_seq)
            {
                if (val == 0xFFFF)
                {
                    gi++;
                    p += 2;
                    continue;
                }
                if (gi == idx)
                    return val;
                p += 2;
            }
            else
            {
                if (gi == idx)
                    return val;
                gi++;
                p += 2;
            }
        }
        return idx;
    }

    {
        const uint8_t *p = font->unicode_data;
        const uint8_t *end = p + font->unicode_bytes;
        uint32_t gi = 0;

        while (p + 4 <= end && gi <= idx)
        {
            uint32_t val = psf__r32le(p);
            if (val == 0xFFFFFFFF)
            {
                gi++;
                p += 4;
                continue;
            }
            if (gi == idx)
                return val;
            p += 4;
        }
        return idx;
    }
}

psf_result psf_parse(const void *data, size_t size, psf_font *font)
{
    if (!data || !font)
        return PSF_ERR_NULL;

    font->raw = (const uint8_t *)data;
    font->raw_size = size;

    if (size < 4)
        return PSF_ERR_SIZE;

    const uint8_t *d = (const uint8_t *)data;

    if (d[0] == PSF1_MAGIC0 && d[1] == PSF1_MAGIC1)
    {
        uint8_t mode = d[2];
        uint8_t charsize = d[3];
        uint32_t nglyphs = (mode & PSF1_MODE512) ? 512u : 256u;
        uint32_t glyph_bytes = nglyphs * charsize;

        if (size < 4u + glyph_bytes)
            return PSF_ERR_SIZE;

        font->version = 1;
        font->glyph_count = nglyphs;
        font->width = 8;
        font->height = charsize;
        font->bytes_per_glyph = charsize;
        font->glyph_data = d + 4;

        if (mode & PSF1_MODEHASTAB)
        {
            font->unicode_data = d + 4u + glyph_bytes;
            font->unicode_bytes = size - 4u - glyph_bytes;
        }
        else
        {
            font->unicode_data = NULL;
            font->unicode_bytes = 0;
        }
        return PSF_OK;
    }

    if (size >= 4 &&
        d[0] == PSF2_MAGIC0 && d[1] == PSF2_MAGIC1 &&
        d[2] == PSF2_MAGIC2 && d[3] == PSF2_MAGIC3)
    {
        if (size < 32)
            return PSF_ERR_SIZE;

        uint32_t ver = psf__r32le(d + 4);
        if (ver != 0)
            return PSF_ERR_VERSION;

        uint32_t headersize = psf__r32le(d + 8);
        uint32_t flags = psf__r32le(d + 12);
        uint32_t numglyph = psf__r32le(d + 16);
        uint32_t bytesperglyph = psf__r32le(d + 20);
        uint32_t height = psf__r32le(d + 24);
        uint32_t width = psf__r32le(d + 28);

        if (headersize < 32 || bytesperglyph == 0 || numglyph == 0)
            return PSF_ERR_SIZE;
        if (width == 0)
            width = 8;

        uint32_t glyph_bytes = numglyph * bytesperglyph;
        if (size < headersize + glyph_bytes)
            return PSF_ERR_SIZE;

        font->version = 2;
        font->glyph_count = numglyph;
        font->width = width;
        font->height = height;
        font->bytes_per_glyph = bytesperglyph;
        font->glyph_data = d + headersize;

        if (flags & PSF2_HAS_UNICODE_TABLE)
        {
            font->unicode_data = d + headersize + glyph_bytes;
            font->unicode_bytes = size - headersize - glyph_bytes;
        }
        else
        {
            font->unicode_data = NULL;
            font->unicode_bytes = 0;
        }
        return PSF_OK;
    }

    return PSF_ERR_MAGIC;
}

psf_result psf_to_tty(psf_font *font, tty_font *tty,
                      tty_glyph *glyphs, uint32_t glyph_cap,
                      uint8_t *pool, size_t pool_cap)
{
    if (!font || !tty || !glyphs || !pool)
        return PSF_ERR_NULL;
    if (font->glyph_count > glyph_cap)
        return PSF_ERR_OVERFLOW;

    uint32_t stride = TTY_STRIDE(font->width);
    size_t per_glyph = stride * font->height;
    size_t need = per_glyph * font->glyph_count;
    if (need > pool_cap)
        return PSF_ERR_POOL;

    psf__memset(pool, 0, need);

    tty->glyph_count = 0;
    tty->width = font->width;
    tty->height = font->height;
    tty->glyphs = glyphs;
    tty->bitmap_pool = pool;
    tty->bitmap_pool_size = need;
    tty->bitmap_pool_cap = pool_cap;

    for (uint32_t i = 0; i < font->glyph_count; i++)
    {
        tty_glyph *g = &glyphs[i];
        const uint8_t *src = font->glyph_data + i * font->bytes_per_glyph;
        uint8_t *dst = pool + i * per_glyph;

        g->codepoint = psf__glyph_codepoint(font, i);
        g->width = font->width;
        g->height = font->height;
        g->x_offset = 0;
        g->y_offset = 0;
        g->bitmap = dst;

        uint32_t src_row = font->bytes_per_glyph / font->height;
        if (src_row > stride)
            src_row = stride;
        for (uint32_t r = 0; r < font->height; r++)
            for (uint32_t b = 0; b < src_row; b++)
                dst[r * stride + b] = src[r * src_row + b];
    }

    tty->glyph_count = font->glyph_count;
    return PSF_OK;
}

const char *psf_result_str(psf_result r)
{
    switch (r)
    {
    case PSF_OK:
        return "success";
    case PSF_ERR_NULL:
        return "null pointer argument";
    case PSF_ERR_MAGIC:
        return "bad magic number";
    case PSF_ERR_VERSION:
        return "unsupported version";
    case PSF_ERR_SIZE:
        return "file too small or malformed";
    case PSF_ERR_OVERFLOW:
        return "glyph array too small";
    case PSF_ERR_POOL:
        return "bitmap pool exhausted";
    default:
        return "unknown error";
    }
}

#endif
#endif
