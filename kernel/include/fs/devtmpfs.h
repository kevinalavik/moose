#ifndef FS_DEVTMPFS_H
#define FS_DEVTMPFS_H

#include <stdint.h>
#include <fs/vfs.h>

#define DEVTMPFS_MAGIC 0xD3F5UL

void devtmpfs_init(void);
int devtmpfs_mknod(const char *name, uint32_t mode, uint32_t dev);

#endif // FS_DEVTMPFS_H