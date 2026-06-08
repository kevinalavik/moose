#ifndef LIB_MATH_H
#define LIB_MATH_H

#include <stdint.h>
#include <util/printf.h>

/* math utils for moose kernel :^() */
#define ALIGN_UP(x, y) (((x) + ((y) - 1)) & ~((y) - 1))
#define ALIGN_DOWN(x, y) ((x) & ~((y) - 1))
#define IS_ALIGNED(x, y) (((x) & ((y) - 1)) == 0)
#define IS_NOT_ALIGNED(x, y) (((x) & ((y) - 1)) != 0)

static inline const char *size_to_str(uint64_t bytes)
{
	static char bufs[4][64];
	static int idx = 0;
	char *buf = bufs[idx++ & 3];

	static const char *units[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB" };

	uint64_t value = bytes;
	uint64_t rem = 0;
	int unit = 0;

	while (value >= 1024 && unit < 5) {
		rem = value & 1023;
		value >>= 10;
		unit++;
	}

	uint64_t frac = (rem * 100) >> 10;

	if (unit == 0)
		ksnprintf(buf, 64, "%llu %s", value, units[unit]);
	else
		ksnprintf(buf, 64, "%llu.%02llu %s", value, frac, units[unit]);

	return buf;
}

#endif /* LIB_MATH_H */