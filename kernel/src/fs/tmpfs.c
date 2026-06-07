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
    char name[256];
    inode_t *inode;
    struct tmpfs_dirent *next;
};

/* inode ops */
static inode_t *tmpfs_lookup(inode_t *dir, const char *name);
static int tmpfs_mkdir(inode_t *dir, const char *name, mode_t mode);
static int tmpfs_create(inode_t *dir, const char *name, mode_t mode, inode_t **out);

/* file ops */
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

    inode->ino = sb->s_ino_counter++;
    inode->mode = mode;
    inode->size = 0;
    inode->nlink = 1;
    inode->sb = sb;
    inode->parent = NULL;
    inode->private_data = NULL;

    if (S_ISDIR(mode))
    {
        inode->i_ops = &tmpfs_dir_iops;
        inode->f_ops = &tmpfs_dir_fops;
    }
    else if (S_ISREG(mode))
    {
        inode->i_ops = NULL;
        inode->f_ops = &tmpfs_reg_fops;
    }
    else
    {
        inode->i_ops = NULL;
        inode->f_ops = NULL;
    }

    return inode;
}

static int tmpfs_add_dirent(inode_t *dir, const char *name, inode_t *child)
{
    struct tmpfs_dirent *ent;

    ent = kmalloc(sizeof(struct tmpfs_dirent));
    if (!ent)
        return -1;

    size_t nlen = strlen(name) + 1;

    if (nlen > sizeof(ent->name))
        nlen = sizeof(ent->name);
    memcpy(ent->name, name, nlen);
    ent->name[nlen - 1] = '\0';
    ent->inode = child;
    ent->next = dir->private_data;
    dir->private_data = ent;

    return 0;
}

static inode_t *tmpfs_lookup(inode_t *dir, const char *name)
{
    struct tmpfs_dirent *ent;

    if (!S_ISDIR(dir->mode))
        return NULL;

    ent = dir->private_data;
    while (ent)
    {
        if (strcmp(ent->name, name) == 0)
            return ent->inode;
        ent = ent->next;
    }

    return NULL;
}

static int tmpfs_mkdir(inode_t *dir, const char *name, mode_t mode)
{
    superblock_t *sb;
    inode_t *child;
    int err;

    if (!S_ISDIR(dir->mode))
        return -1;

    if (tmpfs_lookup(dir, name))
        return -1;

    sb = dir->sb;
    child = tmpfs_alloc_inode(sb, mode | S_IFDIR);
    if (!child)
        return -1;

    child->parent = dir;
    err = tmpfs_add_dirent(dir, name, child);
    if (err)
    {
        kfree(child);
        return err;
    }

    dir->nlink++;

    tmpfs_add_dirent(child, ".", child);
    tmpfs_add_dirent(child, "..", dir);
    child->nlink += 2;
    return 0;
}

static int tmpfs_create(inode_t *dir, const char *name, mode_t mode,
                        inode_t **out)
{
    superblock_t *sb;
    inode_t *child;
    int err;

    if (!S_ISDIR(dir->mode))
        return -1;

    if (tmpfs_lookup(dir, name))
        return -1;

    sb = dir->sb;
    child = tmpfs_alloc_inode(sb, mode | S_IFREG);
    if (!child)
        return -1;

    child->parent = dir;
    err = tmpfs_add_dirent(dir, name, child);
    if (err)
    {
        kfree(child);
        return err;
    }

    if (out)
        *out = child;

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
    inode_t *inode = file->inode;
    size_t avail;

    if (!S_ISREG(inode->mode))
        return -1;

    if (*pos >= (loff_t)inode->size)
        return 0;

    avail = inode->size - (size_t)*pos;
    if (count > avail)
        count = avail;

    if (inode->private_data && count > 0)
        memcpy(buf, (uint8_t *)inode->private_data + *pos, count);

    *pos += count;
    return count;
}

static ssize_t tmpfs_write(file_t *file, const void *buf,
                           size_t count, loff_t *pos)
{
    inode_t *inode = file->inode;
    void *new_data;

    if (!S_ISREG(inode->mode))
        return -1;

    size_t needed = (size_t)*pos + count;
    if (needed > inode->size)
    {
        new_data = kmalloc(needed);
        if (!new_data)
            return -1;

        if (inode->private_data)
        {
            memcpy(new_data, inode->private_data, inode->size);
            kfree(inode->private_data);
        }

        inode->private_data = new_data;
        inode->size = needed;
    }

    memcpy((uint8_t *)inode->private_data + *pos, buf, count);
    if ((size_t)(*pos + count) > inode->size)
        inode->size = (size_t)(*pos + count);

    *pos += count;
    return count;
}

static int tmpfs_readdir(file_t *file, dirent_t *dirent, loff_t *pos)
{
    inode_t *inode = file->inode;
    struct tmpfs_dirent *ent;
    size_t i;

    if (!S_ISDIR(inode->mode))
        return -1;

    ent = inode->private_data;
    i = 0;
    while (ent)
    {
        if (i == (size_t)*pos)
        {
            dirent->d_ino = ent->inode->ino;
            size_t nlen = strlen(ent->name) + 1;

            if (nlen > VFS_DIRENT_NAME_LEN)
                nlen = VFS_DIRENT_NAME_LEN;
            memcpy(dirent->d_name, ent->name, nlen);
            dirent->d_name[nlen - 1] = '\0';
            (*pos)++;
            return 0;
        }
        i++;
        ent = ent->next;
    }

    return -1;
}

static loff_t tmpfs_llseek(file_t *file, loff_t offset, int whence)
{
    inode_t *inode = file->inode;
    loff_t new_pos;

    switch (whence)
    {
    case SEEK_SET:
        new_pos = offset;
        break;
    case SEEK_CUR:
        new_pos = file->pos + offset;
        break;
    case SEEK_END:
        new_pos = (loff_t)inode->size + offset;
        break;
    default:
        return -1;
    }

    if (new_pos < 0)
        return -1;

    file->pos = new_pos;
    return new_pos;
}

superblock_t *tmpfs_mount(void)
{
    superblock_t *sb = kmalloc(sizeof(superblock_t));
    if (!sb)
        return NULL;

    sb->s_ino_counter = 1;
    sb->private_data = NULL;

    inode_t *root = tmpfs_alloc_inode(sb, S_IFDIR | 0755);
    if (!root)
    {
        kfree(sb);
        return NULL;
    }

    root->parent = root;

    tmpfs_add_dirent(root, ".", root);
    tmpfs_add_dirent(root, "..", root);

    sb->s_root = root;

    klog("tmpfs", "mounted");
    return sb;
}