#ifndef LIB_TERM_H
#define LIB_TERM_H

#include <limine.h>
#include <util/bdf.h>

#define ANSI_RESET "\x1b[0m"

#define ANSI_BLACK "\x1b[30m"
#define ANSI_RED "\x1b[31m"
#define ANSI_GREEN "\x1b[32m"
#define ANSI_YELLOW "\x1b[33m"
#define ANSI_BLUE "\x1b[34m"
#define ANSI_MAGENTA "\x1b[35m"
#define ANSI_CYAN "\x1b[36m"
#define ANSI_WHITE "\x1b[37m"

#define ANSI_BOLD_BLACK "\x1b[1;30m"
#define ANSI_BOLD_RED "\x1b[1;31m"
#define ANSI_BOLD_GREEN "\x1b[1;32m"
#define ANSI_BOLD_YELLOW "\x1b[1;33m"
#define ANSI_BOLD_BLUE "\x1b[1;34m"
#define ANSI_BOLD_MAGENTA "\x1b[1;35m"
#define ANSI_BOLD_CYAN "\x1b[1;36m"
#define ANSI_BOLD_WHITE "\x1b[1;37m"

void term_init(struct limine_framebuffer *fb, const BDF_Font *font);

void term_putc(char c);
void term_puts(const char *s);

#endif /* LIB_TERM_H */