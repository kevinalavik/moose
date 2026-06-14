#ifndef LIB_STRING_H
#define LIB_STRING_H

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memmove(void *dst, const void *src, size_t n);
size_t strlen(const char *s);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
int strcmp(const char *a, const char *b);
int isspace(int c);
int strcasecmp(const char *a, const char *b);

#endif // LIB_STRING_H