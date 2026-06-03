#ifndef LIB_TERM_H
#define LIB_TERM_H

#include <limine.h>
#include <util/bdf.h>

/* colors */
#define COL_RESET "\x1b[0m"
#define COL_BLACK "\x1b[30m"
#define COL_RED "\x1b[31m"
#define COL_GREEN "\x1b[32m"
#define COL_YELLOW "\x1b[33m"
#define COL_BLUE "\x1b[34m"
#define COL_MAGENTA "\x1b[35m"
#define COL_CYAN "\x1b[36m"
#define COL_WHITE "\x1b[37m"

/* bright colors */
#define COL_GRAY "\x1b[1;30m"
#define COL_BRED "\x1b[1;31m"
#define COL_BGREEN "\x1b[1;32m"
#define COL_AMBER "\x1b[1;33m"
#define COL_STEEL "\x1b[1;34m"
#define COL_VIOLET "\x1b[1;35m"
#define COL_TEAL "\x1b[1;36m"
#define COL_BRIGHT "\x1b[1;37m"

#define RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))

void term_init(struct limine_framebuffer *fb, const BDF_Font *font);

void term_putc(char c);
void term_puts(const char *s);

#endif /* LIB_TERM_H */