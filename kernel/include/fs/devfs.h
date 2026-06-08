#ifndef FS_DEVFS_H
#define FS_DEVFS_H

#include <fs/vfs.h>
#include <dev/dev.h>

superblock_t *devfs_mount(void);
void devfs_init(void);
int devfs_register(const char *name, handle_t *handle);
int devfs_unregister(const char *name);

#endif /* FS_DEVFS_H */
