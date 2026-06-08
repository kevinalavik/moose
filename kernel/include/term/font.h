#ifndef TERM_FONT_H
#define TERM_FONT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct psf_font {
	uint32_t version;
	uint32_t glyph_count;
	uint32_t width;
	uint32_t height;
	uint32_t bytes_per_glyph;
	const uint8_t *glyph_data;
	const uint8_t *unicode_data;
	size_t unicode_bytes;
	bool mode_has_seq;
} psf_font_t;

bool psf_parse(const void *data, size_t size, psf_font_t *font);
uint32_t psf_glyph_index(const psf_font_t *font, uint32_t cp);

static inline uint32_t psf_row_bytes(const psf_font_t *font)
{
	return font->bytes_per_glyph / font->height;
}

static inline const uint8_t *psf_glyph_row(const psf_font_t *font,
					   uint32_t index, uint32_t row)
{
	return font->glyph_data + index * font->bytes_per_glyph +
	       row * psf_row_bytes(font);
}

#endif /* TERM_FONT_H */