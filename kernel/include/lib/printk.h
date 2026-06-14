#ifndef LIB_PRINTK_H
#define LIB_PRINTK_H

#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>

/* basic printf like kernel printer */
#define PRINTK_NOTIME "\01"
#define PRINTK_FORCE "\02"

extern bool _log_allow_fb;

void printk(const char *fmt, ...);
void vprintk(const char *fmt, va_list ap);

void log(const char *fmt, ...);

int vsnprintk(char *buf, size_t len, const char *fmt, va_list ap);
int snprintk(char *buf, size_t len, const char *fmt, ...);

#endif // LIB_PRINTK_H