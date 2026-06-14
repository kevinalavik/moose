#ifndef ARCH_FASTMEM_H
#define ARCH_FASTMEM_H

/* fast memroy functions using x86_64 instructions, also helpers for framebuffer fast mem (like _row)*/
#include <stddef.h>
#include <stdint.h>

static inline void fast_memcpy_fwd(void *dst, const void *src, size_t n)
{
	void *s = (void *)src, *d = dst;
	size_t qw = n >> 3;
	__asm__ volatile("rep movsq" : "+S"(s), "+D"(d), "+c"(qw)::"memory");
	size_t rem = n & 7;
	if (rem)
		__asm__ volatile("rep movsb" : "+S"(s), "+D"(d), "+c"(rem)::"memory");
}

static inline void fast_memset(void *dst, uint8_t v, size_t n)
{
	void *d = dst;
	uint64_t val = v;
	val |= val << 8;
	val |= val << 16;
	val |= val << 24;
	val |= val << 32;
	size_t qw = n >> 3;
	__asm__ volatile("rep stosq" : "+D"(d), "+c"(qw) : "a"(val) : "memory");
	size_t rem = n & 7;
	if (rem)
		__asm__ volatile("rep stosb" : "+D"(d), "+c"(rem) : "a"(v) : "memory");
}

static inline void fast_memzero(void *dst, size_t n)
{
	fast_memset(dst, 0, n);
}

static inline void fast_memset32(uint32_t *dst, uint32_t v, size_t n)
{
	__asm__ volatile("rep stosl" : "+D"(dst), "+c"(n) : "a"(v) : "memory");
}

static inline void fast_memcpy_row(uint32_t *dst, const uint32_t *src, size_t n)
{
	void *s = (void *)src, *d = dst;
	__asm__ volatile("rep movsl" : "+S"(s), "+D"(d), "+c"(n)::"memory");
}

static inline void fast_memset_row(void *row_ptr, uint32_t c, size_t n_pixels)
{
	fast_memset32((uint32_t *)row_ptr, c, n_pixels);
}

static inline int fast_memcmp(const void *a, const void *b, size_t n)
{
	const uint64_t *x = (const uint64_t *)a;
	const uint64_t *y = (const uint64_t *)b;
	size_t qw = n >> 3;
	for (size_t i = 0; i < qw; i++) {
		if (x[i] != y[i])
			return -1;
	}
	for (size_t i = n & ~7; i < n; i++) {
		int d = ((const uint8_t *)a)[i] - ((const uint8_t *)b)[i];
		if (d)
			return d;
	}
	return 0;
}

#endif // ARCH_FASTMEM_H
