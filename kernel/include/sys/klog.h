#ifndef SYS_KLOG_H
#define SYS_KLOG_H

#include <util/printf.h>
#include <term/ansi.h>
#include <clock/tsc.h>

#define BLIND_MODE 0

#if BLIND_MODE == 1
#define KLOG_COL COL_BRIGHT
#else
#define KLOG_COL COL_GRAY
#endif

#define klog(tag, fmt, ...)                                           \
	do {                                                          \
		uint64_t _ms = tsc_uptime_ms();                       \
		kprintf(KLOG_COL "[%5llu.%03llu] " tag ": " fmt       \
				 "\n" COL_RESET,                      \
			_ms / 1000ULL, _ms % 1000ULL, ##__VA_ARGS__); \
	} while (0)

#endif /* SYS_KLOG_H */
