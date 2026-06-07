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

struct vfs_superblock;
struct vfs_inode;
struct vfs_dirent;
struct vfs_file;

struct vfs_inode_ops
{
    struct vfs_inode *(*lookup)(struct vfs_inode *dir, const char *name);
    int (*mkdir)(struct vfs_inode *dir, const char *name, mode_t mode);
    int (*create)(struct vfs_inode *dir, const char *name, mode_t mode, struct vfs_inode **out);
};

struct vfs_file_ops
{
    ssize_t (*read)(struct vfs_file *file, void *buf, size_t count, loff_t *pos);
    ssize_t (*write)(struct vfs_file *file, const void *buf, size_t count, loff_t *pos);
    int (*readdir)(struct vfs_file *file, struct vfs_dirent *dirent, loff_t *pos);
    int (*open)(struct vfs_inode *inode, struct vfs_file *file);
    int (*release)(struct vfs_inode *inode, struct vfs_file *file);
    loff_t (*llseek)(struct vfs_file *file, loff_t offset, int whence);
};

struct vfs_inode
{
    ino_t ino;
    mode_t mode;
    uint64_t size;
    uint64_t nlink;
    struct vfs_superblock *sb;
    const struct vfs_inode_ops *i_ops;
    const struct vfs_file_ops *f_ops;
    struct vfs_inode *parent;
    void *private_data;
};

struct vfs_superblock
{
    ino_t s_ino_counter;
    struct vfs_inode *s_root;
    void *private_data;
};

struct vfs_dirent
{
    ino_t d_ino;
    char d_name[VFS_DIRENT_NAME_LEN];
};

struct vfs_mount
{
    struct vfs_superblock *sb;
    struct vfs_inode *root;
    struct vfs_mount *next;
    char *path;
};

struct vfs_file
{
    struct vfs_inode *inode;
    const struct vfs_file_ops *f_op;
    loff_t pos;
    int flags;
};

/* VFS core */
void vfs_init(void);
struct vfs_superblock *vfs_mount_root(const char *path,
                                      struct vfs_superblock *sb);

/* Path resolution */
struct vfs_inode *vfs_resolve(struct vfs_inode *root, const char *path);
struct vfs_inode *vfs_lookup(struct vfs_inode *parent, const char *name);

/* Inode operations – dispatch through i_ops */
int vfs_mkdir(struct vfs_inode *parent, const char *name, mode_t mode);
int vfs_create(struct vfs_inode *parent, const char *name, mode_t mode,
               struct vfs_inode **out);
int vfs_mkdir_p(struct vfs_inode *root, const char *path, mode_t mode);

/* File operations – dispatch through f_op */
struct vfs_file *vfs_open(const char *path, int flags);
void vfs_close(struct vfs_file *file);
ssize_t vfs_file_read(struct vfs_file *file, void *buf, size_t count);
ssize_t vfs_file_write(struct vfs_file *file, const void *buf,
                       size_t count);
int vfs_file_readdir(struct vfs_file *file,
                     struct vfs_dirent *dirent);
loff_t vfs_llseek(struct vfs_file *file, loff_t offset, int whence);

/* Raw inode-level helpers – build a temporary file behind the scenes */
ssize_t vfs_read(struct vfs_inode *inode, void *buf, size_t count,
                 off_t offset);
ssize_t vfs_write(struct vfs_inode *inode, const void *buf, size_t count,
                  off_t offset);
int vfs_readdir(struct vfs_inode *inode, struct vfs_dirent *dirent,
                size_t *pos);

#endif /* FS_VFS_H */
