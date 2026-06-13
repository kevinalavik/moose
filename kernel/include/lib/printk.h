#ifndef LIB_PRINTK_H
#define LIB_PRINTK_H


#include <stdarg.h>
#include <stddef.h>

/* basic printf like kernel printer */
void printk(const char *fmt, ...);
void vprintk(const char *fmt, va_list ap);

int vsnprintk(char *buf, size_t len, const char *fmt, va_list ap);
int snprintk(char *buf, size_t len, const char *fmt, ...);

#endif // LIB_PRINTK_H