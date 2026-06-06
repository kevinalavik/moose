#ifndef TERM_TERM_H
#define TERM_TERM_H

#include <limine.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <term/font.h>
#include <term/ansi.h>

#define RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))

struct term
{
    struct limine_framebuffer *fb;
    struct psf_font font;

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

    struct ansi_parser ansi;
};

void term_init(struct term *t, struct limine_framebuffer *fb,
               const void *psf_data, size_t psf_size);
void term_putc(struct term *t, char c);
void term_puts(struct term *t, const char *s);

#endif /* TERM_TERM_H */