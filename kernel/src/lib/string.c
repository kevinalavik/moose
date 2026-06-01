#include <lib/string.h>
#include <stdint.h>

void *memcpy(void *restrict dst, const void *restrict src, size_t n)
{
    uint8_t *restrict d = dst;
    const uint8_t *restrict s = src;

    for (size_t i = 0; i < n; i++)
        d[i] = s[i];

    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;

    for (size_t i = 0; i < n; i++)
        d[i] = (uint8_t)c;

    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;

    if (d == s || n == 0)
        return dst;

    if ((uintptr_t)s > (uintptr_t)d)
    {
        for (size_t i = 0; i < n; i++)
            d[i] = s[i];
    }
    else
    {
        for (size_t i = n; i > 0; i--)
            d[i - 1] = s[i - 1];
    }

    return dst;
}

int memcmp(const void *a_, const void *b_, size_t n)
{
    const uint8_t *a = a_;
    const uint8_t *b = b_;

    for (size_t i = 0; i < n; i++)
    {
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    }

    return 0;
}

size_t strlen(const char *s)
{
    size_t len = 0;

    while (s[len])
        len++;

    return len;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }

    return (unsigned char)*a - (unsigned char)*b;
}