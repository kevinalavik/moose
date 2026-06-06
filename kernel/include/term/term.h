#ifndef TERM_TERM_H
#define TERM_TERM_H

#include <limine.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <term/font.h>
#include <term/ansi.h>

#define RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))

typedef struct term
{
    struct limine_framebuffer *fb;
    psf_font_t font;

    uint32_t cx, cy;
    uint32_t saved_cx, saved_cy;

    uint16_t fg, bg;
    uint32_t fg_rgb, bg_rgb;
    bool fg_is_rgb, bg_is_rgb;
    bool bold, reverse;

    bool wrap;
    bool wrap_pending;
    bool saved_wrap_pending;

    bool cursor_on;
    bool cursor_drawn;

    ansi_parser_t ansi;
} term_t;

void term_init(term_t *t, struct limine_framebuffer *fb,
               const void *psf_data, size_t psf_size);
void term_putc(term_t *t, char c);
void term_puts(term_t *t, const char *s);

#endif /* TERM_TERM_H */