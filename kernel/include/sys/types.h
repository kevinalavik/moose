#ifndef SYS_TYPES_H
#define SYS_TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef uint64_t ino_t;
typedef uint64_t dev_t;
typedef int64_t off_t;
typedef int64_t loff_t;
typedef int64_t ssize_t;
typedef unsigned int mode_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;

#define MKDEV(maj, min) (((dev_t)(maj) << 20) | ((dev_t)(min) & 0xfffff))
#define MAJOR(dev) ((unsigned int)((dev) >> 20))
#define MINOR(dev) ((unsigned int)((dev) & 0xfffff))

#endif /* SYS_TYPES_H */
