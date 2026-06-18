#ifndef FS_VFS_H
#define FS_VFS_H

#include <stdint.h>
#include <stddef.h>
#include <sys/errno.h>
#include <sys/spinlock.h>

typedef struct superblock superblock_t;
typedef struct inode inode_t;
typedef struct dirent dirent_t;
typedef struct file file_t;
typedef struct superblock_ops superblock_ops_t;
typedef struct inode_ops inode_ops_t;
typedef struct file_ops file_ops_t;
typedef struct mount mount_t;

/* todo: maybe move? */
typedef struct cred {
	uint32_t uid; /* effective user  ID */
	uint32_t gid; /* effective group ID */
} cred_t;

/* File type mask and values */
#define S_IFMT 0170000u
#define S_IFSOCK 0140000u
#define S_IFLNK 0120000u
#define S_IFREG 0100000u
#define S_IFBLK 0060000u
#define S_IFDIR 0040000u
#define S_IFCHR 0020000u
#define S_IFIFO 0010000u

/* Permission bits */
#define S_ISUID 0004000u /* set-UID on execute  */
#define S_ISGID 0002000u /* set-GID on execute  */
#define S_ISVTX 0001000u /* sticky bit          */
#define S_IRWXU 0000700u
#define S_IRUSR 0000400u
#define S_IWUSR 0000200u
#define S_IXUSR 0000100u
#define S_IRWXG 0000070u
#define S_IRGRP 0000040u
#define S_IWGRP 0000020u
#define S_IXGRP 0000010u
#define S_IRWXO 0000007u
#define S_IROTH 0000004u
#define S_IWOTH 0000002u
#define S_IXOTH 0000001u

/* Type check helper */
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

/* open flags */
#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR 0x0002
#define O_ACCMODE 0x0003 /* mask for above three */
#define O_CREAT 0x0040
#define O_EXCL 0x0080
#define O_TRUNC 0x0200
#define O_APPEND 0x0400
#define O_DIRECTORY 0x0010000

/* seek flags */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* dirent types */
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12

/* Device number helpers (Linux-compatible encoding) */
#define MKDEV(major, minor) (((uint32_t)(major) << 8) | ((uint32_t)(minor) & 0xff))
#define MAJOR(dev) (((uint32_t)(dev)) >> 8)
#define MINOR(dev) (((uint32_t)(dev)) & 0xff)

#define VFS_NAME_MAX 255  /* max bytes in one path component        */
#define VFS_PATH_MAX 4096 /* max bytes in a full path               */
#define VFS_MAX_MOUNTS 16 /* max simultaneously mounted filesystems */
#define VFS_MAX_FDS 256

struct dirent {
	uint64_t d_ino;                /* inode number                  */
	uint16_t d_reclen;             /* total length of this record   */
	uint8_t d_type;                /* DT_* above                    */
	char d_name[VFS_NAME_MAX + 1]; /* null-terminated component     */
};

struct superblock_ops {
	inode_t *(*alloc_inode)(superblock_t *sb);
	void (*destroy_inode)(superblock_t *sb, inode_t *inode);
	int (*write_inode)(superblock_t *sb, inode_t *inode);
	int (*sync_fs)(superblock_t *sb);
};

struct inode_ops {
	inode_t *(*lookup)(inode_t *dir, const char *name, const cred_t *cred, int *err);
	inode_t *(*create)(
	    inode_t *dir, const char *name, uint32_t mode, const cred_t *cred, int *err);
	inode_t *(*mkdir)(
	    inode_t *dir, const char *name, uint32_t mode, const cred_t *cred, int *err);
	int (*rmdir)(inode_t *dir, const char *name, const cred_t *cred);
	int (*unlink)(inode_t *dir, const char *name, const cred_t *cred);
	int (*rename)(inode_t *old_dir,
	              const char *old_name,
	              inode_t *new_dir,
	              const char *new_name,
	              const cred_t *cred);
	int (*link)(inode_t *dir, const char *name, inode_t *target, const cred_t *cred);
	inode_t *(*symlink)(
	    inode_t *dir, const char *name, const char *target, const cred_t *cred, int *err);
	int (*readlink)(inode_t *inode, char *buf, size_t bufsz);
	int (*truncate)(inode_t *inode, uint64_t size, const cred_t *cred);
	int (*getattr)(inode_t *inode);
	int (*setattr)(
	    inode_t *inode, uint32_t mode, uint32_t uid, uint32_t gid, const cred_t *cred);
	inode_t *(*mknod)(inode_t *dir,
	                  const char *name,
	                  uint32_t mode,
	                  uint32_t dev,
	                  const cred_t *cred,
	                  int *err);
};

struct file_ops {
	int (*open)(file_t *file, const cred_t *cred);
	void (*release)(file_t *file);
	int (*read)(file_t *file, void *buf, size_t count);
	int (*write)(file_t *file, const void *buf, size_t count);
	int64_t (*seek)(file_t *file, int64_t offset, int whence);
	int (*readdir)(file_t *file, int (*emit)(void *ctx, const dirent_t *de), void *ctx);
	int (*ioctl)(file_t *file, uint32_t cmd, void *arg);
};

struct superblock {
	uint64_t s_magic;      /* fs magic (e.g. 0xEF53 for ext2) */
	uint64_t s_block_size; /* native block size in bytes      */
	uint64_t s_blocks_total;
	uint64_t s_blocks_free;
	uint64_t s_inodes_total;
	uint64_t s_inodes_free;
	uint32_t s_max_filename; /* max filename length              */
	uint32_t s_flags;        /* MS_RDONLY etc. (future)          */
	inode_t *s_root;         /* root inode of this filesystem    */
	void *s_fs_info;         /* private fs data (e.g. tmpfs_sb)  */
	superblock_ops_t *s_ops;
	spinlock_t s_lock;
};

struct inode {
	uint64_t i_ino;     /* unique inode number within this fs                 */
	uint32_t i_mode;    /* S_IF* | permissions                                */
	uint32_t i_uid;     /* owner uid                                          */
	uint32_t i_gid;     /* owner gid                                          */
	uint64_t i_size;    /* file size in bytes                                 */
	uint64_t i_atime;   /* last access                                        */
	uint64_t i_mtime;   /* last modify                                        */
	uint64_t i_ctime;   /* last status change                                 */
	uint32_t i_nlink;   /* hard link count (0 meanns this inode can be freed) */
	uint32_t i_blocks;  /* 512-byte blocks allocated                          */
	uint32_t i_rdev;    /* device number (chr/blk nodes only)                 */
	superblock_t *i_sb; /* back-pointer to owning superblock                  */
	void *i_private;    /* fs-specific inode data                             */
	inode_ops_t *i_ops;
	file_ops_t *i_fops;
	spinlock_t i_lock;
};

struct file {
	inode_t *f_inode; /* underlying inode (not owned)                */
	uint64_t f_pos;   /* current read/write byte offset              */
	uint32_t f_flags; /* O_* flags from open()                       */
	uint32_t f_mode;  /* access mode (O_RDONLY, O_WRONLY and so on)  */
	uint32_t f_count; /* reference count (dup, fork, …)              */
	void *f_private;  /* fs/driver-private per-fd state              */
	file_ops_t *f_ops;
	spinlock_t f_lock;
};

struct mount {
	char m_path[VFS_PATH_MAX];
	superblock_t *m_sb;
	int m_active;
};

typedef struct fs_type {
	const char *name;
	int (*mount)(superblock_t *sb, const void *data);
	void (*unmount)(superblock_t *sb);
	struct fs_type *next;
} fs_type_t;

void vfs_init(void);

void vfs_register_fs(fs_type_t *fst);

int vfs_mount(const char *fsname, const char *path, const void *data);
int vfs_umount(const char *path);

inode_t *vfs_resolve(const char *path, const cred_t *cred, int *err);

file_t *vfs_open(const char *path, int flags, uint32_t mode, const cred_t *cred, int *err);
void vfs_close(file_t *file);

int vfs_read(file_t *file, void *buf, size_t count);
int vfs_write(file_t *file, const void *buf, size_t count);

int64_t vfs_seek(file_t *file, int64_t offset, int whence);
int vfs_readdir(file_t *file, int (*emit)(void *ctx, const dirent_t *de), void *ctx);
int vfs_create(const char *path, uint32_t mode, const cred_t *cred);
int vfs_mkdir(const char *path, uint32_t mode, const cred_t *cred);
int vfs_unlink(const char *path, const cred_t *cred);
int vfs_rmdir(const char *path, const cred_t *cred);
int vfs_rename(const char *old_path, const char *new_path, const cred_t *cred);
int vfs_truncate(const char *path, uint64_t size, const cred_t *cred);
int vfs_ioctl(file_t *file, uint32_t cmd, void *arg);
int vfs_mknod(const char *path, uint32_t mode, uint32_t dev, const cred_t *cred);

#endif // FS_VFS_H