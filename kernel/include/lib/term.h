#ifndef LIB_TERM_H
#define LIB_TERM_H

#include <limine.h>
#include <util/bdf.h>

void term_init(struct limine_framebuffer *fb, const BDF_Font *font);

void putc(char c);
void puts(const char *s);

#endif /* LIB_TERM_H */