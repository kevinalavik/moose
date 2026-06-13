#ifndef SYS_PANIC_H
#define SYS_PANIC_H

#include <arch/idt.h>

void panic(int_frame_t *frame, const char *fmt, ...);

#endif // SYS_PANIC_H