#include <lib/string.h>
#include <stdint.h>

void *memcpy(void *restrict dst, const void *restrict src, size_t n)
{
    uint64_t *restrict d64 = (uint64_t *restrict)dst;
    const uint64_t *restrict s64 = (const uint64_t *restrict)src;

    size_t n64 = n / 8;
    for (size_t i = 0; i < n64; i++)
        d64[i] = s64[i];

    for (size_t i = n64 * 8; i < n; i++)
        ((uint8_t *)dst)[i] = ((const uint8_t *)src)[i];

    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    uint64_t pat = (uint8_t)c;
    pat |= pat << 8;
    pat |= pat << 16;
    pat |= pat << 32;

    uint64_t *d64 = (uint64_t *)dst;
    size_t n64 = n / 8;
    for (size_t i = 0; i < n64; i++)
        d64[i] = pat;

    for (size_t i = n64 * 8; i < n; i++)
        ((uint8_t *)dst)[i] = (uint8_t)c;

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
        uint64_t *d64 = (uint64_t *)d;
        const uint64_t *s64 = (const uint64_t *)s;
        size_t n64 = n / 8;
        for (size_t i = 0; i < n64; i++)
            d64[i] = s64[i];
        for (size_t i = n64 * 8; i < n; i++)
            d[i] = s[i];
    }
    else
    {
        uint8_t *d8 = d + n;
        const uint8_t *s8 = s + n;
        while (n >= 8)
        {
            d8 -= 8;
            s8 -= 8;
            *(uint64_t *)d8 = *(const uint64_t *)s8;
            n -= 8;
        }
        while (n--)
        {
            d8--;
            s8--;
            *d8 = *s8;
        }
    }

    return dst;
}

int memcmp(const void *a_, const void *b_, size_t n)
{
    const uint64_t *a64 = (const uint64_t *)a_;
    const uint64_t *b64 = (const uint64_t *)b_;

    size_t n64 = n / 8;
    for (size_t i = 0; i < n64; i++)
    {
        if (a64[i] != b64[i])
        {
            const uint8_t *a8 = (const uint8_t *)&a64[i];
            const uint8_t *b8 = (const uint8_t *)&b64[i];
            for (size_t j = 0; j < 8; j++)
            {
                if (a8[j] != b8[j])
                    return a8[j] < b8[j] ? -1 : 1;
            }
        }
    }

    for (size_t i = n64 * 8; i < n; i++)
    {
        const uint8_t *a8 = (const uint8_t *)a_;
        const uint8_t *b8 = (const uint8_t *)b_;
        if (a8[i] != b8[i])
            return a8[i] < b8[i] ? -1 : 1;
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

char *strcpy(char *restrict dst, const char *restrict src)
{
    char *ret = dst;

    while ((*dst++ = *src++))
        ;

    return ret;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;

    while (*s)
    {
        if (*s == (char)c)
            last = s;
        s++;
    }

    return (char *)last;
}
