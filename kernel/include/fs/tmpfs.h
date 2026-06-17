#ifndef FS_TMPFS_H
#define FS_TMPFS_H

#include <fs/vfs.h>

#define TMPFS_MAGIC                                                                                \
	0x01021994 /* took straight from https://github.com/torvalds/linux/blob/master/include/uapi/linux/magic.h */

#define TMPFS_MAX_DIRENTS 512

typedef struct tmpfs_sb {
	uint64_t next_ino;    /* monotonically increasing inode counter  */
	uint64_t bytes_used;  /* total bytes of file data currently held */
	uint64_t bytes_limit; /* 0 = unlimited, otherwise a soft cap     */
} tmpfs_sb_t;

typedef struct tmpfs_dirent {
	char name[VFS_NAME_MAX + 1];
	inode_t *inode;
} tmpfs_dirent_t;


typedef struct tmpfs_inode {
	/* file */
	uint8_t *data;
	size_t data_cap;  /* allocated capacity in bytes */
	size_t data_size; /* local file size in bytes  */

	/* dir */
	tmpfs_dirent_t entries[TMPFS_MAX_DIRENTS];
	size_t entry_count;

	/* symlink */
	char *link_target;
} tmpfs_inode_t;

void tmpfs_init(void);

#endif // FS_TMPFS_H