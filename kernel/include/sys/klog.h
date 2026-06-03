#ifndef SYS_KLOG_H
#define SYS_KLOG_H

#include <util/printf.h>
#include <lib/term.h>

#define klog(tag, fmt, ...) \
    kprintf("[" tag "] " fmt "\n", ##__VA_ARGS__)

#endif /* SYS_KLOG_H */