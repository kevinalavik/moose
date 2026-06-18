#ifndef DEV_CHRDEV_H
#define DEV_CHRDEV_H

#include <stdint.h>
#include <fs/vfs.h>

#define CHRDEV_MAX_MAJOR 256
#define CHRDEV_NODEV 0

typedef struct chrdev_entry {
	const char *name;
	file_ops_t *fops;
} chrdev_entry_t;

int chrdev_register(uint32_t major, const char *name, file_ops_t *fops);
int chrdev_unregister(uint32_t major);
file_ops_t *chrdev_lookup(uint32_t major);
void chrdev_init(void);

#endif // DEV_CHRDEV_H