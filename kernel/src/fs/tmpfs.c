#include <fs/tmpfs.h>
#include <fs/vfs.h>
#include <sys/klog.h>
#include <mm/kheap.h>
#include <lib/string.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

struct tmpfs_dirent
{
    char td_name[VFS_NAME_MAX + 1];
    inode_t *td_inode;
    struct tmpfs_dirent *td_next;
};

static inode_t *tmpfs_lookup(inode_t *dir, const char *name);
static int tmpfs_mkdir(inode_t *dir, const char *name, mode_t mode);
static int tmpfs_create(inode_t *dir, const char *name, mode_t mode, inode_t **out);
static int tmpfs_mknod(inode_t *dir, const char *name, mode_t mode, dev_t rdev, inode_t **out);
static int tmpfs_getattr(inode_t *inode, stat_t *st);
static int tmpfs_open(inode_t *inode, file_t *file);
static int tmpfs_release(inode_t *inode, file_t *file);
static ssize_t tmpfs_read(file_t *file, void *buf, size_t count, loff_t *pos);
static ssize_t tmpfs_write(file_t *file, const void *buf, size_t count, loff_t *pos);
static int tmpfs_readdir(file_t *file, dirent_t *dirent, loff_t *pos);
static loff_t tmpfs_llseek(file_t *file, loff_t offset, int whence);

static const inode_ops_t tmpfs_dir_iops = {
    .lookup = tmpfs_lookup,
    .mkdir = tmpfs_mkdir,
    .create = tmpfs_create,
    .mknod = tmpfs_mknod,
    .getattr = tmpfs_getattr,
};

static const file_ops_t tmpfs_dir_fops = {
    .open = tmpfs_open,
    .release = tmpfs_release,
    .readdir = tmpfs_readdir,
    .llseek = tmpfs_llseek,
};

static const file_ops_t tmpfs_reg_fops = {
    .open = tmpfs_open,
    .release = tmpfs_release,
    .read = tmpfs_read,
    .write = tmpfs_write,
    .llseek = tmpfs_llseek,
};

static inode_t *tmpfs_alloc_inode(superblock_t *sb, mode_t mode)
{
    inode_t *inode = kmalloc(sizeof(inode_t));
    if (!inode)
        return NULL;

    inode->i_ino = sb->s_ino_next++;
    inode->i_mode = mode;
    inode->i_size = 0;
    inode->i_nlink = 1;
    inode->i_rdev = 0;
    inode->i_sb = sb;
    inode->i_parent = NULL;
    inode->i_private = NULL;

    if (S_ISDIR(mode))
    {
        inode->i_ops = &tmpfs_dir_iops;
        inode->i_fop = &tmpfs_dir_fops;
    }
    else if (S_ISREG(mode))
    {
        inode->i_ops = NULL;
        inode->i_fop = &tmpfs_reg_fops;
    }
    else
    {
        inode->i_ops = NULL;
        inode->i_fop = NULL;
    }

    return inode;
}

static int tmpfs_add_dirent(inode_t *dir, const char *name, inode_t *child)
{
    struct tmpfs_dirent *ent = kmalloc(sizeof(struct tmpfs_dirent));
    if (!ent)
        return -1;

    size_t nlen = strlen(name);
    if (nlen > VFS_NAME_MAX)
        nlen = VFS_NAME_MAX;
    memcpy(ent->td_name, name, nlen);
    ent->td_name[nlen] = '\0';
    ent->td_inode = child;
    ent->td_next = dir->i_private;
    dir->i_private = ent;
    return 0;
}

static inode_t *tmpfs_lookup(inode_t *dir, const char *name)
{
    if (!S_ISDIR(dir->i_mode))
        return NULL;
    for (struct tmpfs_dirent *e = dir->i_private; e; e = e->td_next)
        if (strcmp(e->td_name, name) == 0)
            return e->td_inode;
    return NULL;
}

static int tmpfs_mkdir(inode_t *dir, const char *name, mode_t mode)
{
    if (!S_ISDIR(dir->i_mode) || tmpfs_lookup(dir, name))
        return -1;

    inode_t *child = tmpfs_alloc_inode(dir->i_sb, S_IFDIR | (mode & ~S_IFMT));
    if (!child)
        return -1;

    child->i_parent = dir;

    if (tmpfs_add_dirent(dir, name, child) || tmpfs_add_dirent(child, ".", child) || tmpfs_add_dirent(child, "..", dir))
    {
        kfree(child);
        return -1;
    }

    child->i_nlink += 2;
    dir->i_nlink++;
    return 0;
}

static int tmpfs_create(inode_t *dir, const char *name, mode_t mode, inode_t **out)
{
    if (!S_ISDIR(dir->i_mode) || tmpfs_lookup(dir, name))
        return -1;

    inode_t *child = tmpfs_alloc_inode(dir->i_sb, S_IFREG | (mode & ~S_IFMT));
    if (!child)
        return -1;

    child->i_parent = dir;

    if (tmpfs_add_dirent(dir, name, child))
    {
        kfree(child);
        return -1;
    }

    if (out)
        *out = child;
    return 0;
}

static int tmpfs_mknod(inode_t *dir, const char *name, mode_t mode, dev_t rdev, inode_t **out)
{
    if (!S_ISDIR(dir->i_mode) || tmpfs_lookup(dir, name))
        return -1;

    inode_t *child = tmpfs_alloc_inode(dir->i_sb, mode);
    if (!child)
        return -1;

    child->i_rdev = rdev;
    child->i_parent = dir;

    if (tmpfs_add_dirent(dir, name, child))
    {
        kfree(child);
        return -1;
    }

    if (out)
        *out = child;
    return 0;
}

static int tmpfs_getattr(inode_t *inode, stat_t *st)
{
    st->st_ino = inode->i_ino;
    st->st_mode = inode->i_mode;
    st->st_nlink = inode->i_nlink;
    st->st_size = inode->i_size;
    st->st_rdev = inode->i_rdev;
    return 0;
}

static int tmpfs_open(inode_t *inode, file_t *file)
{
    (void)inode;
    (void)file;
    return 0;
}

static int tmpfs_release(inode_t *inode, file_t *file)
{
    (void)inode;
    (void)file;
    return 0;
}

static ssize_t tmpfs_read(file_t *file, void *buf, size_t count, loff_t *pos)
{
    inode_t *inode = file->f_inode;

    if (!S_ISREG(inode->i_mode))
        return -1;
    if (*pos >= (loff_t)inode->i_size)
        return 0;

    size_t avail = inode->i_size - (size_t)*pos;
    if (count > avail)
        count = avail;

    if (inode->i_private && count > 0)
        memcpy(buf, (uint8_t *)inode->i_private + *pos, count);

    *pos += (loff_t)count;
    return (ssize_t)count;
}

static ssize_t tmpfs_write(file_t *file, const void *buf, size_t count,
                           loff_t *pos)
{
    inode_t *inode = file->f_inode;

    if (!S_ISREG(inode->i_mode))
        return -1;

    size_t needed = (size_t)*pos + count;
    if (needed > inode->i_size)
    {
        void *new_data = kmalloc(needed);
        if (!new_data)
            return -1;
        if (inode->i_private)
        {
            memcpy(new_data, inode->i_private, inode->i_size);
            kfree(inode->i_private);
        }
        inode->i_private = new_data;
        inode->i_size = needed;
    }

    memcpy((uint8_t *)inode->i_private + *pos, buf, count);
    *pos += (loff_t)count;
    return (ssize_t)count;
}

static int tmpfs_readdir(file_t *file, dirent_t *dirent, loff_t *pos)
{
    inode_t *inode = file->f_inode;

    if (!S_ISDIR(inode->i_mode))
        return -1;

    loff_t i = 0;
    for (struct tmpfs_dirent *e = inode->i_private; e; e = e->td_next, i++)
    {
        if (i == *pos)
        {
            dirent->d_ino = e->td_inode->i_ino;
            dirent->d_type = vfs_mode_to_dtype(e->td_inode->i_mode);
            size_t nlen = strlen(e->td_name);
            if (nlen > VFS_NAME_MAX)
                nlen = VFS_NAME_MAX;
            memcpy(dirent->d_name, e->td_name, nlen);
            dirent->d_name[nlen] = '\0';
            (*pos)++;
            return 0;
        }
    }

    return 1;
}

static loff_t tmpfs_llseek(file_t *file, loff_t offset, int whence)
{
    inode_t *inode = file->f_inode;
    loff_t new_pos;

    switch (whence)
    {
    case SEEK_SET:
        new_pos = offset;
        break;
    case SEEK_CUR:
        new_pos = file->f_pos + offset;
        break;
    case SEEK_END:
        new_pos = (loff_t)inode->i_size + offset;
        break;
    default:
        return -1;
    }

    if (new_pos < 0)
        return -1;

    file->f_pos = new_pos;
    return new_pos;
}

superblock_t *tmpfs_mount(void)
{
    superblock_t *sb = kmalloc(sizeof(superblock_t));
    if (!sb)
        return NULL;

    sb->s_ino_next = 1;
    sb->s_private = NULL;

    inode_t *root = tmpfs_alloc_inode(sb, S_IFDIR | 0755);
    if (!root)
    {
        kfree(sb);
        return NULL;
    }

    root->i_parent = root;
    tmpfs_add_dirent(root, ".", root);
    tmpfs_add_dirent(root, "..", root);
    root->i_nlink += 2;

    sb->s_root = root;
    klog("tmpfs", "mounted");
    return sb;
}
