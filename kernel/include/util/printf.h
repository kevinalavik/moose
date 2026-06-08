#ifndef UTIL_PRINTF_H
#define UTIL_PRINTF_H

#include <stdarg.h>
#include <stddef.h>

/* basic printf like implementation for moose (not complete) */
#define PRINTF_PREFIX(f) k##f

int PRINTF_PREFIX(vprintf)(const char *fmt, va_list vlist);
int PRINTF_PREFIX(vsnprintf)(char *buf, size_t size, const char *fmt,
			     va_list args);
int PRINTF_PREFIX(snprintf)(char *buffer, size_t size, const char *fmt, ...);
int PRINTF_PREFIX(printf)(const char *fmt, ...);

#endif /* UTIL_PRINTF_H */