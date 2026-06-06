#include <term/font.h>

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

static uint32_t r32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool psf_parse(const void *data, size_t size, struct psf_font *font)
{
    if (!data || !font || size < 4)
        return false;

    const uint8_t *d = (const uint8_t *)data;

    if (d[0] == PSF1_MAGIC0 && d[1] == PSF1_MAGIC1)
    {
        uint8_t mode = d[2];
        uint8_t charsize = d[3];
        uint32_t nglyphs = (mode & PSF1_MODE512) ? 512u : 256u;
        uint32_t glyph_bytes = nglyphs * charsize;

        if (size < 4u + glyph_bytes)
            return false;

        font->version = 1;
        font->glyph_count = nglyphs;
        font->width = 8;
        font->height = charsize;
        font->bytes_per_glyph = charsize;
        font->glyph_data = d + 4;
        font->mode_has_seq = (mode & PSF1_MODEHASSEQ) != 0;

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
        return true;
    }

    if (size >= 4 &&
        d[0] == PSF2_MAGIC0 && d[1] == PSF2_MAGIC1 &&
        d[2] == PSF2_MAGIC2 && d[3] == PSF2_MAGIC3)
    {
        if (size < 32)
            return false;

        uint32_t headersize = r32le(d + 8);
        uint32_t flags = r32le(d + 12);
        uint32_t numglyph = r32le(d + 16);
        uint32_t bytesperglyph = r32le(d + 20);
        uint32_t height = r32le(d + 24);
        uint32_t width = r32le(d + 28);

        if (headersize < 32 || bytesperglyph == 0 || numglyph == 0)
            return false;
        if (width == 0)
            width = 8;

        uint32_t glyph_bytes = numglyph * bytesperglyph;
        if (size < headersize + glyph_bytes)
            return false;

        font->version = 2;
        font->glyph_count = numglyph;
        font->width = width;
        font->height = height;
        font->bytes_per_glyph = bytesperglyph;
        font->glyph_data = d + headersize;
        font->mode_has_seq = false;

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
        return true;
    }

    return false;
}

uint32_t psf_glyph_index(const struct psf_font *font, uint32_t cp)
{
    if (!font->unicode_data || font->unicode_bytes == 0)
    {
        if (cp < font->glyph_count)
            return cp;
        return 0;
    }

    if (font->version == 1)
    {
        const uint8_t *p = font->unicode_data;
        const uint8_t *end = p + font->unicode_bytes;

        if (font->mode_has_seq)
        {

            uint32_t gi = 0;
            while (p + 2 <= end && gi < font->glyph_count)
            {
                uint16_t val = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
                if (val == 0xFFFF)
                {
                    gi++;
                    p += 2;
                    continue;
                }
                if (val == cp)
                    return gi;
                p += 2;
            }
        }
        else
        {

            uint32_t gi = 0;
            while (p + 2 <= end && gi < font->glyph_count)
            {
                uint16_t val = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
                if (val == cp)
                    return gi;
                gi++;
                p += 2;
            }
        }
    }
    else
    {

        const uint8_t *p = font->unicode_data;
        const uint8_t *end = p + font->unicode_bytes;
        uint32_t gi = 0;

        while (p + 4 <= end && gi < font->glyph_count)
        {
            uint32_t val = r32le(p);
            if (val == 0xFFFFFFFF)
            {
                gi++;
                p += 4;
                continue;
            }
            if (val == cp)
                return gi;
            p += 4;
        }
    }

    if (cp < font->glyph_count)
        return cp;
    return 0;
}
