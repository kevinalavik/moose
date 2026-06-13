#ifndef LIB_KCONSOLE_H
#define LIB_KCONSOLE_H

#include <limine.h>

#define KCONSOLE_DEFAULT_FG 0x00B4B4B4
#define KCONSOLE_DEFAULT_BG 0x00000000

void kconsole_init(struct limine_framebuffer *f);
void kconsole_write(const char *s);

void kconsole_set_fg(uint32_t rgb);
void kconsole_set_bg(uint32_t rgb);
void kconsole_draw_bar(void);

#endif // LIB_KCONSOLE_H