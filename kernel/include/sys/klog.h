#ifndef SYS_KLOG_H
#define SYS_KLOG_H

#include <util/printf.h>
#include <term/ansi.h>
#include <dev/tsc.h>

#define klog(tag, fmt, ...)                                             \
    do                                                                  \
    {                                                                   \
        uint64_t _ms = tsc_uptime_ms();                                 \
        kprintf(COL_GRAY "[%5llu.%03llu] " tag ": " fmt "\n" COL_RESET, \
                _ms / 1000ULL, _ms % 1000ULL, ##__VA_ARGS__);           \
    } while (0)

#endif /* SYS_KLOG_H */