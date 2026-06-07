#include <fs/vfs.h>
#include <sys/klog.h>
#include <mm/kheap.h>
#include <lib/string.h>
#include <stdint.h>
#include <stddef.h>

static struct vfs_mount *mount_list = NULL;

void vfs_init(void)
{
    klog("vfs", "initialized");
}

struct vfs_superblock *vfs_mount_root(const char *path, struct vfs_superblock *sb)
{
    struct vfs_mount *mnt = kmalloc(sizeof(struct vfs_mount));
    if (!mnt)
        return NULL;

    size_t plen = strlen(path) + 1;
    mnt->path = kmalloc(plen);
    if (!mnt->path)
    {
        kfree(mnt);
        return NULL;
    }
    memcpy(mnt->path, path, plen);
    mnt->sb = sb;
    mnt->root = sb->s_root;
    mnt->next = NULL;

    if (!mount_list)
        mount_list = mnt;
    else
    {
        struct vfs_mount *last = mount_list;
        while (last->next)
            last = last->next;
        last->next = mnt;
    }

    klog("vfs", "mounted on %s", path);
    return sb;
}

struct vfs_inode *vfs_resolve(struct vfs_inode *root, const char *path)
{
    struct vfs_inode *cur = root;
    const char *p = path;
    char component[256];

    if (!cur || !p)
        return NULL;

    if (p[0] == '/')
    {
        while (*p == '/')
            p++;
        if (!*p)
            return cur;
    }

    while (*p)
    {
        size_t i = 0;
        while (*p && *p != '/' && i < sizeof(component) - 1)
            component[i++] = *p++;
        component[i] = '\0';

        if (!cur->i_ops || !cur->i_ops->lookup)
            return NULL;

        cur = cur->i_ops->lookup(cur, component);
        if (!cur)
            return NULL;

        while (*p == '/')
            p++;
    }

    return cur;
}

struct vfs_inode *vfs_lookup(struct vfs_inode *parent, const char *name)
{
    if (!parent || !name || !parent->i_ops || !parent->i_ops->lookup)
        return NULL;

    return parent->i_ops->lookup(parent, name);
}

int vfs_mkdir(struct vfs_inode *parent, const char *name, mode_t mode)
{
    if (!parent || !name || !parent->i_ops || !parent->i_ops->mkdir)
        return -1;

    return parent->i_ops->mkdir(parent, name, mode);
}

int vfs_create(struct vfs_inode *parent, const char *name, mode_t mode,
               struct vfs_inode **out)
{
    if (!parent || !name || !parent->i_ops || !parent->i_ops->create)
        return -1;

    return parent->i_ops->create(parent, name, mode, out);
}

int vfs_mkdir_p(struct vfs_inode *root, const char *path, mode_t mode)
{
    struct vfs_inode *cur;
    struct vfs_inode *existing;
    char buf[256];
    (void)mode; /* todo: support mode */

    if (!root || !path)
        return -1;

    existing = vfs_resolve(root, path);
    if (existing)
    {
        if (S_ISDIR(existing->mode))
            return 0;
        return -1;
    }

    cur = root;
    while (*path == '/')
        path++;

    if (!*path)
        return 0;

    while (*path)
    {
        size_t i = 0;

        while (*path && *path != '/' && i < sizeof(buf) - 1)
            buf[i++] = *path++;
        buf[i] = '\0';

        if (buf[0] == '\0')
            continue;

        if (cur->i_ops && cur->i_ops->lookup)
        {
            existing = cur->i_ops->lookup(cur, buf);
            if (existing)
            {
                cur = existing;
                while (*path == '/')
                    path++;
                continue;
            }
        }

        if (cur->i_ops && cur->i_ops->mkdir)
        {
            int err = cur->i_ops->mkdir(cur, buf, S_IFDIR | 0755);

            if (err)
                return err;

            existing = cur->i_ops->lookup(cur, buf);
            if (!existing)
                return -1;
            cur = existing;
        }

        while (*path == '/')
            path++;
    }

    return 0;
}

struct vfs_file *vfs_open(const char *path, int flags)
{
    struct vfs_inode *inode;

    if (!mount_list)
        return NULL;

    if (path[0] == '/')
        inode = vfs_resolve(mount_list->root, path);
    else
        inode = mount_list->root;

    if (!inode)
        return NULL;

    struct vfs_file *file = kmalloc(sizeof(struct vfs_file));
    if (!file)
        return NULL;

    file->inode = inode;
    file->f_op = inode->f_ops;
    file->pos = 0;
    file->flags = flags;

    if (file->f_op && file->f_op->open)
        file->f_op->open(inode, file);

    return file;
}

void vfs_close(struct vfs_file *file)
{
    if (!file)
        return;

    if (file->f_op && file->f_op->release)
        file->f_op->release(file->inode, file);

    kfree(file);
}

ssize_t vfs_file_read(struct vfs_file *file, void *buf, size_t count)
{
    if (!file || !buf || !file->f_op || !file->f_op->read)
        return -1;

    return file->f_op->read(file, buf, count, &file->pos);
}

ssize_t vfs_file_write(struct vfs_file *file, const void *buf, size_t count)
{
    if (!file || !buf || !file->f_op || !file->f_op->write)
        return -1;

    return file->f_op->write(file, buf, count, &file->pos);
}

int vfs_file_readdir(struct vfs_file *file, struct vfs_dirent *dirent)
{
    if (!file || !dirent || !file->f_op || !file->f_op->readdir)
        return -1;

    return file->f_op->readdir(file, dirent, &file->pos);
}

loff_t vfs_llseek(struct vfs_file *file, loff_t offset, int whence)
{
    if (!file || !file->f_op || !file->f_op->llseek)
        return -1;

    return file->f_op->llseek(file, offset, whence);
}

ssize_t vfs_read(struct vfs_inode *inode, void *buf, size_t count,
                 off_t offset)
{
    if (!inode || !inode->f_ops || !inode->f_ops->read)
        return -1;

    struct vfs_file tmp;

    tmp.inode = inode;
    tmp.f_op = inode->f_ops;
    tmp.pos = offset;
    tmp.flags = O_RDONLY;

    loff_t pos = offset;
    return inode->f_ops->read(&tmp, buf, count, &pos);
}

ssize_t vfs_write(struct vfs_inode *inode, const void *buf, size_t count,
                  off_t offset)
{
    if (!inode || !inode->f_ops || !inode->f_ops->write)
        return -1;

    struct vfs_file tmp;

    tmp.inode = inode;
    tmp.f_op = inode->f_ops;
    tmp.pos = offset;
    tmp.flags = O_WRONLY;

    loff_t pos = offset;
    return inode->f_ops->write(&tmp, buf, count, &pos);
}

int vfs_readdir(struct vfs_inode *inode, struct vfs_dirent *dirent,
                size_t *pos)
{
    if (!inode || !dirent || !pos ||
        !inode->f_ops || !inode->f_ops->readdir)
        return -1;

    struct vfs_file tmp;

    tmp.inode = inode;
    tmp.f_op = inode->f_ops;
    tmp.pos = 0;
    tmp.flags = O_RDONLY;

    loff_t loff = (loff_t)*pos;
    int ret = inode->f_ops->readdir(&tmp, dirent, &loff);

    *pos = (size_t)loff;
    return ret;
}
