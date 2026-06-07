#include <fs/vfs.h>
#include <sys/klog.h>
#include <mm/kheap.h>
#include <lib/string.h>
#include <stdint.h>
#include <stddef.h>

static mount_t *mount_list = NULL;

void vfs_init(void)
{
    klog("vfs", "initialized");
}

superblock_t *vfs_mount_root(const char *path, superblock_t *sb)
{
    mount_t *mnt = kmalloc(sizeof(mount_t));
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
        mount_t *last = mount_list;
        while (last->next)
            last = last->next;
        last->next = mnt;
    }

    klog("vfs", "mounted on %s", path);
    return sb;
}

inode_t *vfs_resolve(inode_t *root, const char *path)
{
    inode_t *cur = root;
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

inode_t *vfs_lookup(inode_t *parent, const char *name)
{
    if (!parent || !name || !parent->i_ops || !parent->i_ops->lookup)
        return NULL;

    return parent->i_ops->lookup(parent, name);
}

int vfs_mkdir(inode_t *parent, const char *name, mode_t mode)
{
    if (!parent || !name || !parent->i_ops || !parent->i_ops->mkdir)
        return -1;

    return parent->i_ops->mkdir(parent, name, mode);
}

int vfs_create(inode_t *parent, const char *name, mode_t mode,
               inode_t **out)
{
    if (!parent || !name || !parent->i_ops || !parent->i_ops->create)
        return -1;

    return parent->i_ops->create(parent, name, mode, out);
}

int vfs_mkdir_p(inode_t *root, const char *path, mode_t mode)
{
    inode_t *cur;
    inode_t *existing;
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

file_t *vfs_open(const char *path, int flags)
{
    inode_t *inode;

    if (!mount_list)
        return NULL;

    if (path[0] == '/')
        inode = vfs_resolve(mount_list->root, path);
    else
        inode = mount_list->root;

    if (!inode)
        return NULL;

    file_t *file = kmalloc(sizeof(file_t));
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

void vfs_close(file_t *file)
{
    if (!file)
        return;

    if (file->f_op && file->f_op->release)
        file->f_op->release(file->inode, file);

    kfree(file);
}

ssize_t vfs_file_read(file_t *file, void *buf, size_t count)
{
    if (!file || !buf || !file->f_op || !file->f_op->read)
        return -1;

    return file->f_op->read(file, buf, count, &file->pos);
}

ssize_t vfs_file_write(file_t *file, const void *buf, size_t count)
{
    if (!file || !buf || !file->f_op || !file->f_op->write)
        return -1;

    return file->f_op->write(file, buf, count, &file->pos);
}

int vfs_file_readdir(file_t *file, dirent_t *dirent)
{
    if (!file || !dirent || !file->f_op || !file->f_op->readdir)
        return -1;

    return file->f_op->readdir(file, dirent, &file->pos);
}

loff_t vfs_llseek(file_t *file, loff_t offset, int whence)
{
    if (!file || !file->f_op || !file->f_op->llseek)
        return -1;

    return file->f_op->llseek(file, offset, whence);
}

ssize_t vfs_read(inode_t *inode, void *buf, size_t count,
                 off_t offset)
{
    if (!inode || !inode->f_ops || !inode->f_ops->read)
        return -1;

    file_t tmp;

    tmp.inode = inode;
    tmp.f_op = inode->f_ops;
    tmp.pos = offset;
    tmp.flags = O_RDONLY;

    loff_t pos = offset;
    return inode->f_ops->read(&tmp, buf, count, &pos);
}

ssize_t vfs_write(inode_t *inode, const void *buf, size_t count,
                  off_t offset)
{
    if (!inode || !inode->f_ops || !inode->f_ops->write)
        return -1;

    file_t tmp;

    tmp.inode = inode;
    tmp.f_op = inode->f_ops;
    tmp.pos = offset;
    tmp.flags = O_WRONLY;

    loff_t pos = offset;
    return inode->f_ops->write(&tmp, buf, count, &pos);
}

int vfs_readdir(inode_t *inode, dirent_t *dirent,
                size_t *pos)
{
    if (!inode || !dirent || !pos ||
        !inode->f_ops || !inode->f_ops->readdir)
        return -1;

    file_t tmp;

    tmp.inode = inode;
    tmp.f_op = inode->f_ops;
    tmp.pos = 0;
    tmp.flags = O_RDONLY;

    loff_t loff = (loff_t)*pos;
    int ret = inode->f_ops->readdir(&tmp, dirent, &loff);

    *pos = (size_t)loff;
    return ret;
}