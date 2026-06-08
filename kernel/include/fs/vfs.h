#ifndef FS_VFS_H
#define FS_VFS_H

#include <sys/types.h>

#define S_IFMT 0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFBLK 0060000
#define S_IFLNK 0120000
#define S_IFIFO 0010000

#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)

#define S_IRWXU 00700
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRWXG 00070
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IRWXO 00007
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10

#define VFS_NAME_MAX 255
#define VFS_PATH_MAX 4096

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct inode inode_t;
typedef struct superblock superblock_t;
typedef struct mount mount_t;
typedef struct dirent dirent_t;
typedef struct file file_t;
typedef struct stat stat_t;

struct stat
{
    ino_t st_ino;
    mode_t st_mode;
    uid_t st_uid;
    gid_t st_gid;
    uint64_t st_nlink;
    uint64_t st_size;
    dev_t st_rdev;
};

typedef struct inode_ops
{
    inode_t *(*lookup)(inode_t *dir, const char *name);
    int (*mkdir)(inode_t *dir, const char *name, mode_t mode);
    int (*create)(inode_t *dir, const char *name, mode_t mode, inode_t **out);
    int (*getattr)(inode_t *inode, stat_t *stat);
    int (*mknod)(inode_t *dir, const char *name, mode_t mode, dev_t rdev, inode_t **out);
} inode_ops_t;

typedef struct file_ops
{
    int (*open)(inode_t *inode, file_t *file);
    int (*release)(inode_t *inode, file_t *file);
    ssize_t (*read)(file_t *file, void *buf, size_t count, loff_t *pos);
    ssize_t (*write)(file_t *file, const void *buf, size_t count, loff_t *pos);
    int (*readdir)(file_t *file, dirent_t *dirent, loff_t *pos);
    loff_t (*llseek)(file_t *file, loff_t offset, int whence);
} file_ops_t;

struct inode
{
    ino_t i_ino;
    mode_t i_mode;
    uid_t i_uid;
    gid_t i_gid;
    uint64_t i_size;
    uint64_t i_nlink;
    dev_t i_rdev;
    superblock_t *i_sb;
    const inode_ops_t *i_ops;
    const file_ops_t *i_fop;
    struct inode *i_parent;
    void *i_private;
};

struct superblock
{
    ino_t s_ino_next;
    inode_t *s_root;
    void *s_private;
};

struct dirent
{
    ino_t d_ino;
    uint8_t d_type;
    char d_name[VFS_NAME_MAX + 1];
};

struct mount
{
    superblock_t *mnt_sb;
    inode_t *mnt_root;
    mount_t *mnt_next;
    char *mnt_path;
};

struct file
{
    inode_t *f_inode;
    const file_ops_t *f_op;
    loff_t f_pos;
    int f_flags;
};

superblock_t *vfs_mount(const char *path, superblock_t *sb);
inode_t *vfs_resolve(inode_t *root, const char *path);
inode_t *vfs_lookup(inode_t *parent, const char *name);
inode_t *vfs_inode(const char *path);
int vfs_mkdir(inode_t *parent, const char *name, mode_t mode);
int vfs_mkdir_p(inode_t *root, const char *path, mode_t mode);
int vfs_mknod(inode_t *parent, const char *name, mode_t mode, dev_t rdev, inode_t **out);
int vfs_create(inode_t *parent, const char *name, mode_t mode, inode_t **out);
file_t *vfs_open(const char *path, int flags);
void vfs_close(file_t *file);
ssize_t vfs_read(file_t *file, void *buf, size_t count);
ssize_t vfs_write(file_t *file, const void *buf, size_t count);
int vfs_readdir(file_t *file, dirent_t *dirent);
loff_t vfs_llseek(file_t *file, loff_t offset, int whence);
int vfs_stat(const char *path, stat_t *st);
int vfs_fstat(file_t *file, stat_t *st);
int vfs_chmod(const char *path, mode_t mode);
int vfs_chown(const char *path, uid_t uid, gid_t gid);

static inline uint8_t vfs_mode_to_dtype(mode_t mode)
{
    switch (mode & S_IFMT)
    {
    case S_IFREG:
        return DT_REG;
    case S_IFDIR:
        return DT_DIR;
    case S_IFCHR:
        return DT_CHR;
    case S_IFBLK:
        return DT_BLK;
    case S_IFLNK:
        return DT_LNK;
    case S_IFIFO:
        return DT_FIFO;
    default:
        return DT_UNKNOWN;
    }
}

#endif /* FS_VFS_H */