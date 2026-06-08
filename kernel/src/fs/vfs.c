#include <fs/vfs.h>
#include <sys/klog.h>
#include <mm/kheap.h>
#include <lib/string.h>
#include <stdint.h>
#include <stddef.h>

static mount_t *mount_list = NULL;

superblock_t *vfs_mount(const char *path, superblock_t *sb)
{
    if (!path || !sb)
        return NULL;

    mount_t *mnt = kmalloc(sizeof(mount_t));
    if (!mnt)
        return NULL;

    size_t plen = strlen(path) + 1;
    mnt->mnt_path = kmalloc(plen);
    if (!mnt->mnt_path)
    {
        kfree(mnt);
        return NULL;
    }
    memcpy(mnt->mnt_path, path, plen);
    mnt->mnt_sb = sb;
    mnt->mnt_root = sb->s_root;
    mnt->mnt_next = NULL;

    if (!mount_list)
    {
        mount_list = mnt;
    }
    else
    {
        mount_t *last = mount_list;
        while (last->mnt_next)
            last = last->mnt_next;
        last->mnt_next = mnt;
    }

    klog("vfs", "mounted %s", path);
    return sb;
}

inode_t *vfs_resolve(inode_t *root, const char *path)
{
    inode_t *cur = root;
    const char *p = path;
    char component[VFS_NAME_MAX + 1];

    if (!cur || !p)
        return NULL;

    if (*p == '/')
    {
        while (*p == '/')
            p++;
        if (!*p)
            return cur;
    }

    while (*p)
    {
        size_t i = 0;
        while (*p && *p != '/' && i < VFS_NAME_MAX)
            component[i++] = *p++;
        component[i] = '\0';

        if (i == 0)
        {
            while (*p == '/')
                p++;
            continue;
        }

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

int vfs_mkdir_p(inode_t *root, const char *path, mode_t mode)
{
    inode_t *cur, *existing;
    char buf[VFS_NAME_MAX + 1];

    if (!root || !path)
        return -1;

    existing = vfs_resolve(root, path);
    if (existing)
        return S_ISDIR(existing->i_mode) ? 0 : -1;

    cur = root;
    while (*path == '/')
        path++;

    if (!*path)
        return 0;

    while (*path)
    {
        size_t i = 0;
        while (*path && *path != '/' && i < VFS_NAME_MAX)
            buf[i++] = *path++;
        buf[i] = '\0';

        if (i == 0)
        {
            while (*path == '/')
                path++;
            continue;
        }

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

        if (!cur->i_ops || !cur->i_ops->mkdir)
            return -1;

        int err = cur->i_ops->mkdir(cur, buf, S_IFDIR | mode);
        if (err)
            return err;

        cur = cur->i_ops->lookup(cur, buf);
        if (!cur)
            return -1;

        while (*path == '/')
            path++;
    }

    return 0;
}

int vfs_create(inode_t *parent, const char *name, mode_t mode, inode_t **out)
{
    if (!parent || !name || !parent->i_ops || !parent->i_ops->create)
        return -1;
    return parent->i_ops->create(parent, name, mode, out);
}

int vfs_mknod(inode_t *parent, const char *name, mode_t mode, dev_t rdev,
              inode_t **out)
{
    if (!parent || !name || !parent->i_ops || !parent->i_ops->mknod)
        return -1;
    return parent->i_ops->mknod(parent, name, mode, rdev, out);
}

static inode_t *vfs_path_resolve(const char *path)
{
    mount_t *best = NULL;
    size_t best_len = 0;

    for (mount_t *m = mount_list; m; m = m->mnt_next)
    {
        size_t mlen = strlen(m->mnt_path);
        if (mlen < best_len)
            continue;
        if (strcmp(m->mnt_path, "/") == 0)
        {
            if (mlen > best_len)
            {
                best = m;
                best_len = mlen;
            }
        }
        else if (strncmp(path, m->mnt_path, mlen) == 0 && (path[mlen] == '/' || path[mlen] == '\0'))
        {
            if (mlen > best_len)
            {
                best = m;
                best_len = mlen;
            }
        }
    }

    if (!best)
        return NULL;

    const char *rel = path + best_len;
    while (*rel == '/')
        rel++;

    return vfs_resolve(best->mnt_root, rel);
}

inode_t *vfs_inode(const char *path)
{
    if (!path || !mount_list)
        return NULL;
    return vfs_path_resolve(path);
}

file_t *vfs_open(const char *path, int flags)
{
    inode_t *inode;

    if (!mount_list || !path)
        return NULL;

    inode = vfs_path_resolve(path);
    if (!inode)
        return NULL;

    file_t *file = kmalloc(sizeof(file_t));
    if (!file)
        return NULL;

    file->f_inode = inode;
    file->f_op = inode->i_fop;
    file->f_pos = 0;
    file->f_flags = flags;

    if (file->f_op && file->f_op->open)
        file->f_op->open(inode, file);

    return file;
}

void vfs_close(file_t *file)
{
    if (!file)
        return;
    if (file->f_op && file->f_op->release)
        file->f_op->release(file->f_inode, file);
    kfree(file);
}

ssize_t vfs_read(file_t *file, void *buf, size_t count)
{
    if (!file || !buf || !file->f_op || !file->f_op->read)
        return -1;
    return file->f_op->read(file, buf, count, &file->f_pos);
}

ssize_t vfs_write(file_t *file, const void *buf, size_t count)
{
    if (!file || !buf || !file->f_op || !file->f_op->write)
        return -1;
    return file->f_op->write(file, buf, count, &file->f_pos);
}

int vfs_readdir(file_t *file, dirent_t *dirent)
{
    if (!file || !dirent || !file->f_op || !file->f_op->readdir)
        return -1;
    return file->f_op->readdir(file, dirent, &file->f_pos);
}

loff_t vfs_llseek(file_t *file, loff_t offset, int whence)
{
    if (!file || !file->f_op || !file->f_op->llseek)
        return -1;
    return file->f_op->llseek(file, offset, whence);
}

static int inode_getattr(inode_t *inode, stat_t *st)
{
    if (!inode || !st)
        return -1;
    if (inode->i_ops && inode->i_ops->getattr)
        return inode->i_ops->getattr(inode, st);
    st->st_ino = inode->i_ino;
    st->st_mode = inode->i_mode;
    st->st_nlink = inode->i_nlink;
    st->st_size = inode->i_size;
    st->st_rdev = inode->i_rdev;
    return 0;
}

int vfs_stat(const char *path, stat_t *st)
{
    inode_t *inode;

    if (!path || !st || !mount_list)
        return -1;

    inode = vfs_path_resolve(path);
    if (!inode)
        return -1;

    return inode_getattr(inode, st);
}

int vfs_fstat(file_t *file, stat_t *st)
{
    if (!file || !st)
        return -1;
    return inode_getattr(file->f_inode, st);
}