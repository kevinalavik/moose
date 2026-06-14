#include <lib/string.h>
#include <stdint.h>
#include <arch/fastmem.h>
#include <mm/heap.h>

void *memcpy(void *dst, const void *src, size_t n)
{
	fast_memcpy_fwd(dst, src, n);
	return dst;
}

void *memset(void *s, int c, size_t n)
{
	fast_memset(s, (uint8_t)c, n);
	return s;
}

int memcmp(const void *a, const void *b, size_t n)
{
	return fast_memcmp(a, b, n);
}

void *memmove(void *dst, const void *src, size_t n)
{
	uint8_t *d = (uint8_t *)dst;
	const uint8_t *s = (const uint8_t *)src;

	if (d == s || n == 0)
		return dst;

	if (d < s) {
		fast_memcpy_fwd(dst, src, n);
	} else {
		for (size_t i = n; i > 0; i--)
			d[i - 1] = s[i - 1];
	}

	return dst;
}

size_t strlen(const char *s)
{
	size_t i = 0;
	while (s[i])
		i++;
	return i;
}

char *strcpy(char *dst, const char *src)
{
	char *ret = dst;

	while ((*dst++ = *src++) != '\0')
		;

	return ret;
}

char *strncpy(char *dst, const char *src, size_t n)
{
	size_t i = 0;

	for (; i < n && src[i]; i++)
		dst[i] = src[i];

	for (; i < n; i++)
		dst[i] = '\0';

	return dst;
}

int strcmp(const char *a, const char *b)
{
	while (*a && (*a == *b)) {
		a++;
		b++;
	}

	return (unsigned char)(*a) - (unsigned char)(*b);
}

int isspace(int c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

int strcasecmp(const char *a, const char *b)
{
	while (*a && *b) {
		char ca = *a;
		char cb = *b;
		if (ca >= 'A' && ca <= 'Z')
			ca += 0x20;
		if (cb >= 'A' && cb <= 'Z')
			cb += 0x20;
		if (ca != cb)
			return (unsigned char)ca - (unsigned char)cb;
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

char *strdup(const char *s)
{
	size_t len;
	char *out;

	if (!s)
		return NULL;

	len = strlen(s) + 1;

	out = kmalloc(len);
	if (!out)
		return NULL;

	memcpy(out, s, len);

	return out;
}

int strncmp(const char *a, const char *b, size_t n)
{
	size_t i = 0;

	if (n == 0)
		return 0;

	while (i < n && a[i] && b[i]) {
		if (a[i] != b[i])
			return (unsigned char)a[i] - (unsigned char)b[i];
		i++;
	}

	if (i == n)
		return 0;

	return (unsigned char)a[i] - (unsigned char)b[i];
}

char *strstr(const char *haystack, const char *needle)
{
	size_t i, j;

	if (!*needle)
		return (char *)haystack;

	for (i = 0; haystack[i]; i++) {
		for (j = 0; needle[j]; j++) {
			if (haystack[i + j] != needle[j])
				break;
		}

		if (!needle[j])
			return (char *)&haystack[i];
	}

	return NULL;
}