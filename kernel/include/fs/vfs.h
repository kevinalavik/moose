#ifndef FS_VFS_H
#define FS_VFS_H

#include <sys/types.h>

#define S_IFMT 0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFBLK 0060000
#define S_IFLNK 0120000

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

#define VFS_DIRENT_NAME_LEN 256

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

typedef struct inode_ops
{
    inode_t *(*lookup)(inode_t *dir, const char *name);
    int (*mkdir)(inode_t *dir, const char *name, mode_t mode);
    int (*create)(inode_t *dir, const char *name, mode_t mode, inode_t **out);
} inode_ops_t;

typedef struct file_ops
{
    ssize_t (*read)(file_t *file, void *buf, size_t count, loff_t *pos);
    ssize_t (*write)(file_t *file, const void *buf, size_t count, loff_t *pos);
    int (*readdir)(file_t *file, dirent_t *dirent, loff_t *pos);
    int (*open)(inode_t *inode, file_t *file);
    int (*release)(inode_t *inode, file_t *file);
    loff_t (*llseek)(file_t *file, loff_t offset, int whence);
} file_ops_t;

typedef struct inode
{
    ino_t ino;
    mode_t mode;
    uint64_t size;
    uint64_t nlink;
    superblock_t *sb;
    const inode_ops_t *i_ops;
    const file_ops_t *f_ops;
    struct inode *parent;
    void *private_data;
} inode_t;

typedef struct superblock
{
    ino_t s_ino_counter;
    inode_t *s_root;
    void *private_data;
} superblock_t;

typedef struct dirent
{
    ino_t d_ino;
    char d_name[VFS_DIRENT_NAME_LEN];
} dirent_t;

typedef struct mount
{
    superblock_t *sb;
    inode_t *root;
    mount_t *next;
    char *path;
} mount_t;

typedef struct file
{
    inode_t *inode;
    const file_ops_t *f_op;
    loff_t pos;
    int flags;
} file_t;

void vfs_init(void);
superblock_t *vfs_mount_root(const char *path, superblock_t *sb);
inode_t *vfs_resolve(inode_t *root, const char *path);
inode_t *vfs_lookup(inode_t *parent, const char *name);
int vfs_mkdir(inode_t *parent, const char *name, mode_t mode);
int vfs_create(inode_t *parent, const char *name, mode_t mode,
               inode_t **out);
int vfs_mkdir_p(inode_t *root, const char *path, mode_t mode);
file_t *vfs_open(const char *path, int flags);
void vfs_close(file_t *file);
ssize_t vfs_file_read(file_t *file, void *buf, size_t count);
ssize_t vfs_file_write(file_t *file, const void *buf, size_t count);
int vfs_file_readdir(file_t *file, dirent_t *dirent);
loff_t vfs_llseek(file_t *file, loff_t offset, int whence);
ssize_t vfs_read(inode_t *inode, void *buf, size_t count, off_t offset);
ssize_t vfs_write(inode_t *inode, const void *buf, size_t count, off_t offset);
int vfs_readdir(inode_t *inode, dirent_t *dirent, size_t *pos);

#endif /* FS_VFS_H */