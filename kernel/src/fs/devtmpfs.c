#include <fs/devtmpfs.h>
#include <fs/tmpfs.h>
#include <fs/vfs.h>
#include <dev/chrdev.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/printk.h>
#include <sys/errno.h>
#include <sys/panic.h>

static inode_t *devtmpfs_alloc_inode(superblock_t *sb);
static void devtmpfs_destroy_inode(superblock_t *sb, inode_t *inode);
static int devtmpfs_write_inode(superblock_t *sb, inode_t *inode);
static int devtmpfs_sync_fs(superblock_t *sb);
static inode_t *devtmpfs_lookup(inode_t *dir, const char *name, const cred_t *cred, int *err);
static inode_t *
devtmpfs_create(inode_t *dir, const char *name, uint32_t mode, const cred_t *cred, int *err);
static inode_t *
devtmpfs_mkdir(inode_t *dir, const char *name, uint32_t mode, const cred_t *cred, int *err);
static int devtmpfs_rmdir(inode_t *dir, const char *name, const cred_t *cred);
static int devtmpfs_unlink(inode_t *dir, const char *name, const cred_t *cred);
static int devtmpfs_rename(inode_t *old_dir,
                           const char *old_name,
                           inode_t *new_dir,
                           const char *new_name,
                           const cred_t *cred);
static int devtmpfs_link(inode_t *dir, const char *name, inode_t *target, const cred_t *cred);
static inode_t *
devtmpfs_symlink(inode_t *dir, const char *name, const char *target, const cred_t *cred, int *err);
static int devtmpfs_readlink(inode_t *inode, char *buf, size_t bufsz);
static int devtmpfs_truncate(inode_t *inode, uint64_t size, const cred_t *cred);
static int devtmpfs_getattr(inode_t *inode);
static int
devtmpfs_setattr(inode_t *inode, uint32_t mode, uint32_t uid, uint32_t gid, const cred_t *cred);
static inode_t *devtmpfs_mknod_op(
    inode_t *dir, const char *name, uint32_t mode, uint32_t dev, const cred_t *cred, int *err);
static int devtmpfs_chrdev_open(file_t *file, const cred_t *cred);
static void devtmpfs_chrdev_release(file_t *file);
static int devtmpfs_chrdev_read(file_t *file, void *buf, size_t count);
static int devtmpfs_chrdev_write(file_t *file, const void *buf, size_t count);
static int devtmpfs_chrdev_ioctl(file_t *file, uint32_t cmd, void *arg);

static int devtmpfs_mount_cb(superblock_t *sb, const void *data);
static void devtmpfs_unmount_cb(superblock_t *sb);

static superblock_ops_t devtmpfs_sb_ops = {
    .alloc_inode = devtmpfs_alloc_inode,
    .destroy_inode = devtmpfs_destroy_inode,
    .write_inode = devtmpfs_write_inode,
    .sync_fs = devtmpfs_sync_fs,
};

static inode_ops_t devtmpfs_inode_ops = {
    .lookup = devtmpfs_lookup,
    .create = devtmpfs_create,
    .mkdir = devtmpfs_mkdir,
    .rmdir = devtmpfs_rmdir,
    .unlink = devtmpfs_unlink,
    .rename = devtmpfs_rename,
    .link = devtmpfs_link,
    .symlink = devtmpfs_symlink,
    .readlink = devtmpfs_readlink,
    .truncate = devtmpfs_truncate,
    .getattr = devtmpfs_getattr,
    .setattr = devtmpfs_setattr,
    .mknod = devtmpfs_mknod_op,
};

static file_ops_t devtmpfs_chrdev_fops = {
    .open = devtmpfs_chrdev_open,
    .release = devtmpfs_chrdev_release,
    .read = devtmpfs_chrdev_read,
    .write = devtmpfs_chrdev_write,
    .seek = NULL,
    .readdir = NULL,
    .ioctl = devtmpfs_chrdev_ioctl,
};

static fs_type_t devtmpfs_fs_type = {
    .name = "devtmpfs",
    .mount = devtmpfs_mount_cb,
    .unmount = devtmpfs_unmount_cb,
    .next = NULL,
};

static inode_t *
alloc_dev_inode(superblock_t *sb, uint32_t mode, uint32_t uid, uint32_t gid, uint32_t rdev)
{
	tmpfs_sb_t *tsb = (tmpfs_sb_t *)sb->s_fs_info;

	inode_t *inode = kmalloc(sizeof(inode_t));
	if (!inode)
		return NULL;
	memset(inode, 0, sizeof(*inode));
	spin_init(&inode->i_lock);

	tmpfs_inode_t *ti = kmalloc(sizeof(tmpfs_inode_t));
	if (!ti) {
		kfree(inode);
		return NULL;
	}
	memset(ti, 0, sizeof(*ti));

	inode->i_ino = tsb->next_ino++;
	inode->i_mode = mode;
	inode->i_uid = uid;
	inode->i_gid = gid;
	inode->i_nlink = 1;
	inode->i_rdev = rdev;
	inode->i_sb = sb;
	inode->i_private = ti;
	inode->i_ops = &devtmpfs_inode_ops;

	if (S_ISDIR(mode)) {
		inode->i_fops = NULL;
	} else if (S_ISCHR(mode) || S_ISBLK(mode)) {
		inode->i_fops = &devtmpfs_chrdev_fops;
	} else {
		inode->i_fops = NULL;
	}

	return inode;
}

static inode_t *dir_find(inode_t *dir, const char *name)
{
	tmpfs_inode_t *td = (tmpfs_inode_t *)dir->i_private;
	for (size_t i = 0; i < td->entry_count; i++) {
		if (strcmp(td->entries[i].name, name) == 0)
			return td->entries[i].inode;
	}
	return NULL;
}

static int dir_add(inode_t *dir, const char *name, inode_t *child)
{
	tmpfs_inode_t *td = (tmpfs_inode_t *)dir->i_private;

	if (td->entry_count >= TMPFS_MAX_DIRENTS)
		return -ENOSPC;

	size_t nlen = strlen(name);
	if (nlen > VFS_NAME_MAX)
		return -ENAMETOOLONG;

	tmpfs_dirent_t *de = &td->entries[td->entry_count];
	memcpy(de->name, name, nlen + 1);
	de->inode = child;
	td->entry_count++;
	return 0;
}

static int dir_remove(inode_t *dir, const char *name)
{
	tmpfs_inode_t *td = (tmpfs_inode_t *)dir->i_private;
	for (size_t i = 0; i < td->entry_count; i++) {
		if (strcmp(td->entries[i].name, name) == 0) {
			for (size_t j = i; j + 1 < td->entry_count; j++)
				td->entries[j] = td->entries[j + 1];
			td->entry_count--;
			return 0;
		}
	}
	return -ENOENT;
}

static void free_inode(superblock_t *sb, inode_t *inode)
{
	if (!inode)
		return;
	tmpfs_inode_t *ti = (tmpfs_inode_t *)inode->i_private;
	(void)sb;
	if (ti) {
		if (S_ISREG(inode->i_mode) && ti->data)
			kfree(ti->data);
		if (S_ISLNK(inode->i_mode) && ti->link_target)
			kfree(ti->link_target);
		kfree(ti);
	}
	kfree(inode);
}

static void free_inode_tree_dev(superblock_t *sb, inode_t *inode)
{
	if (!inode)
		return;
	tmpfs_inode_t *ti = (tmpfs_inode_t *)inode->i_private;
	if (S_ISDIR(inode->i_mode)) {
		for (size_t i = 0; i < ti->entry_count; i++) {
			const char *n = ti->entries[i].name;
			if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0)
				continue;
			free_inode_tree_dev(sb, ti->entries[i].inode);
		}
	} else if (S_ISREG(inode->i_mode)) {
		if (ti->data)
			kfree(ti->data);
	} else if (S_ISLNK(inode->i_mode)) {
		if (ti->link_target)
			kfree(ti->link_target);
	}
	kfree(ti);
	kfree(inode);
}

static inode_t *devtmpfs_alloc_inode(superblock_t *sb)
{
	return alloc_dev_inode(sb, 0, 0, 0, 0);
}

static void devtmpfs_destroy_inode(superblock_t *sb, inode_t *inode)
{
	free_inode(sb, inode);
}

static int devtmpfs_write_inode(superblock_t *sb, inode_t *inode)
{
	(void)sb;
	(void)inode;
	return 0;
}

static int devtmpfs_sync_fs(superblock_t *sb)
{
	(void)sb;
	return 0;
}

static inode_t *devtmpfs_lookup(inode_t *dir, const char *name, const cred_t *cred, int *err)
{
	(void)cred;
	if (!S_ISDIR(dir->i_mode)) {
		if (err)
			*err = -ENOTDIR;
		return NULL;
	}
	inode_t *child = dir_find(dir, name);
	if (!child) {
		if (err)
			*err = -ENOENT;
		return NULL;
	}
	return child;
}

static inode_t *
devtmpfs_create(inode_t *dir, const char *name, uint32_t mode, const cred_t *cred, int *err)
{
	if (!S_ISDIR(dir->i_mode)) {
		if (err)
			*err = -ENOTDIR;
		return NULL;
	}
	if (dir_find(dir, name)) {
		if (err)
			*err = -EEXIST;
		return NULL;
	}
	inode_t *inode =
	    alloc_dev_inode(dir->i_sb, (mode & ~S_IFMT) | S_IFREG, cred->uid, cred->gid, 0);
	if (!inode) {
		if (err)
			*err = -ENOMEM;
		return NULL;
	}
	int ret = dir_add(dir, name, inode);
	if (ret < 0) {
		free_inode(dir->i_sb, inode);
		if (err)
			*err = ret;
		return NULL;
	}
	return inode;
}

static inode_t *
devtmpfs_mkdir(inode_t *dir, const char *name, uint32_t mode, const cred_t *cred, int *err)
{
	if (!S_ISDIR(dir->i_mode)) {
		if (err)
			*err = -ENOTDIR;
		return NULL;
	}
	if (dir_find(dir, name)) {
		if (err)
			*err = -EEXIST;
		return NULL;
	}
	inode_t *inode =
	    alloc_dev_inode(dir->i_sb, (mode & ~S_IFMT) | S_IFDIR, cred->uid, cred->gid, 0);
	if (!inode) {
		if (err)
			*err = -ENOMEM;
		return NULL;
	}

	tmpfs_inode_t *ti = (tmpfs_inode_t *)inode->i_private;
	memcpy(ti->entries[0].name, ".", 2);
	ti->entries[0].inode = inode;
	memcpy(ti->entries[1].name, "..", 3);
	ti->entries[1].inode = dir;
	ti->entry_count = 2;

	inode->i_nlink = 2;
	dir->i_nlink++;

	int ret = dir_add(dir, name, inode);
	if (ret < 0) {
		free_inode(dir->i_sb, inode);
		if (err)
			*err = ret;
		return NULL;
	}
	return inode;
}

static int devtmpfs_rmdir(inode_t *dir, const char *name, const cred_t *cred)
{
	(void)cred;
	inode_t *child = dir_find(dir, name);
	if (!child)
		return -ENOENT;
	if (!S_ISDIR(child->i_mode))
		return -ENOTDIR;
	tmpfs_inode_t *tc = (tmpfs_inode_t *)child->i_private;
	if (tc->entry_count > 2)
		return -ENOTEMPTY;
	dir_remove(dir, name);
	dir->i_nlink--;
	free_inode(dir->i_sb, child);
	return 0;
}

static int devtmpfs_unlink(inode_t *dir, const char *name, const cred_t *cred)
{
	(void)cred;
	inode_t *child = dir_find(dir, name);
	if (!child)
		return -ENOENT;
	if (S_ISDIR(child->i_mode))
		return -EISDIR;
	dir_remove(dir, name);
	child->i_nlink--;
	if (child->i_nlink == 0)
		free_inode(dir->i_sb, child);
	return 0;
}

static int devtmpfs_rename(inode_t *old_dir,
                           const char *old_name,
                           inode_t *new_dir,
                           const char *new_name,
                           const cred_t *cred)
{
	(void)cred;
	inode_t *src = dir_find(old_dir, old_name);
	if (!src)
		return -ENOENT;
	inode_t *dst = dir_find(new_dir, new_name);
	if (dst) {
		if (S_ISDIR(dst->i_mode)) {
			tmpfs_inode_t *td = (tmpfs_inode_t *)dst->i_private;
			if (td->entry_count > 2)
				return -ENOTEMPTY;
		}
		dir_remove(new_dir, new_name);
		dst->i_nlink--;
		if (dst->i_nlink == 0)
			free_inode(new_dir->i_sb, dst);
	}
	int ret = dir_add(new_dir, new_name, src);
	if (ret < 0)
		return ret;
	dir_remove(old_dir, old_name);
	return 0;
}

static int devtmpfs_link(inode_t *dir, const char *name, inode_t *target, const cred_t *cred)
{
	(void)cred;
	if (S_ISDIR(target->i_mode))
		return -EPERM;
	if (dir_find(dir, name))
		return -EEXIST;
	int ret = dir_add(dir, name, target);
	if (ret < 0)
		return ret;
	target->i_nlink++;
	return 0;
}

static inode_t *
devtmpfs_symlink(inode_t *dir, const char *name, const char *target, const cred_t *cred, int *err)
{
	if (dir_find(dir, name)) {
		if (err)
			*err = -EEXIST;
		return NULL;
	}
	inode_t *inode = alloc_dev_inode(dir->i_sb, S_IFLNK | 0777, cred->uid, cred->gid, 0);
	if (!inode) {
		if (err)
			*err = -ENOMEM;
		return NULL;
	}
	tmpfs_inode_t *ti = (tmpfs_inode_t *)inode->i_private;
	ti->link_target = strdup(target);
	if (!ti->link_target) {
		free_inode(dir->i_sb, inode);
		if (err)
			*err = -ENOMEM;
		return NULL;
	}
	inode->i_size = strlen(target);
	int ret = dir_add(dir, name, inode);
	if (ret < 0) {
		free_inode(dir->i_sb, inode);
		if (err)
			*err = ret;
		return NULL;
	}
	return inode;
}

static int devtmpfs_readlink(inode_t *inode, char *buf, size_t bufsz)
{
	if (!S_ISLNK(inode->i_mode))
		return -EINVAL;
	tmpfs_inode_t *ti = (tmpfs_inode_t *)inode->i_private;
	if (!ti->link_target)
		return -EIO;
	size_t len = strlen(ti->link_target);
	if (len + 1 > bufsz)
		len = bufsz - 1;
	memcpy(buf, ti->link_target, len);
	buf[len] = '\0';
	return (int)len;
}

static int devtmpfs_truncate(inode_t *inode, uint64_t size, const cred_t *cred)
{
	(void)cred;
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		return -EINVAL;
	if (!S_ISREG(inode->i_mode))
		return -EINVAL;
	tmpfs_inode_t *ti = (tmpfs_inode_t *)inode->i_private;
	tmpfs_sb_t *tsb = (tmpfs_sb_t *)inode->i_sb->s_fs_info;
	if (size == 0) {
		if (ti->data) {
			tsb->bytes_used -= ti->data_size;
			kfree(ti->data);
			ti->data = NULL;
			ti->data_cap = 0;
			ti->data_size = 0;
		}
		inode->i_size = 0;
		inode->i_blocks = 0;
		return 0;
	}
	if (size > ti->data_cap) {
		uint8_t *nd = kmalloc((size_t)size);
		if (!nd)
			return -ENOMEM;
		if (ti->data) {
			memcpy(nd, ti->data, ti->data_size);
			kfree(ti->data);
		}
		memset(nd + ti->data_size, 0, (size_t)(size - ti->data_size));
		tsb->bytes_used += (size - ti->data_size);
		ti->data = nd;
		ti->data_cap = (size_t)size;
	} else if (size < ti->data_size) {
		tsb->bytes_used -= (ti->data_size - size);
	}
	ti->data_size = (size_t)size;
	inode->i_size = size;
	inode->i_blocks = (uint32_t)((size + 511) / 512);
	return 0;
}

static int devtmpfs_getattr(inode_t *inode)
{
	(void)inode;
	return 0;
}

static int
devtmpfs_setattr(inode_t *inode, uint32_t mode, uint32_t uid, uint32_t gid, const cred_t *cred)
{
	if (cred->uid != 0 && cred->uid != inode->i_uid)
		return -EPERM;
	inode->i_mode = (inode->i_mode & S_IFMT) | (mode & ~S_IFMT);
	inode->i_uid = uid;
	inode->i_gid = gid;
	return 0;
}

static inode_t *devtmpfs_mknod_op(
    inode_t *dir, const char *name, uint32_t mode, uint32_t dev, const cred_t *cred, int *err)
{
	if (!S_ISDIR(dir->i_mode)) {
		if (err)
			*err = -ENOTDIR;
		return NULL;
	}
	if (!S_ISCHR(mode) && !S_ISBLK(mode) && !S_ISFIFO(mode)) {
		if (err)
			*err = -EINVAL;
		return NULL;
	}
	if (dir_find(dir, name)) {
		if (err)
			*err = -EEXIST;
		return NULL;
	}

	inode_t *inode = alloc_dev_inode(dir->i_sb, mode, cred->uid, cred->gid, dev);
	if (!inode) {
		if (err)
			*err = -ENOMEM;
		return NULL;
	}

	int ret = dir_add(dir, name, inode);
	if (ret < 0) {
		free_inode(dir->i_sb, inode);
		if (err)
			*err = ret;
		return NULL;
	}

	log("devtmpfs: mknod '%s' mode=0%o dev=%u:%u\n", name, mode, MAJOR(dev), MINOR(dev));
	return inode;
}

static int devtmpfs_dir_open(file_t *file, const cred_t *cred)
{
	(void)file;
	(void)cred;
	return 0;
}

static void devtmpfs_dir_release(file_t *file)
{
	(void)file;
}

static int devtmpfs_dir_readdir(file_t *file, int (*emit)(void *ctx, const dirent_t *de), void *ctx)
{
	inode_t *inode = file->f_inode;
	tmpfs_inode_t *ti = (tmpfs_inode_t *)inode->i_private;

	while (file->f_pos < ti->entry_count) {
		tmpfs_dirent_t *tde = &ti->entries[(size_t)file->f_pos];
		file->f_pos++;

		dirent_t de;
		memset(&de, 0, sizeof(de));
		de.d_ino = tde->inode->i_ino;
		de.d_reclen = sizeof(de);

		uint32_t m = tde->inode->i_mode;
		if (S_ISREG(m))
			de.d_type = DT_REG;
		else if (S_ISDIR(m))
			de.d_type = DT_DIR;
		else if (S_ISLNK(m))
			de.d_type = DT_LNK;
		else if (S_ISCHR(m))
			de.d_type = DT_CHR;
		else if (S_ISBLK(m))
			de.d_type = DT_BLK;
		else if (S_ISFIFO(m))
			de.d_type = DT_FIFO;
		else if (S_ISSOCK(m))
			de.d_type = DT_SOCK;
		else
			de.d_type = DT_UNKNOWN;

		strncpy(de.d_name, tde->name, VFS_NAME_MAX);
		de.d_name[VFS_NAME_MAX] = '\0';

		int ret = emit(ctx, &de);
		if (ret != 0)
			return ret;
	}
	return 0;
}

static file_ops_t devtmpfs_dir_fops = {
    .open = devtmpfs_dir_open,
    .release = devtmpfs_dir_release,
    .read = NULL,
    .write = NULL,
    .seek = NULL,
    .readdir = devtmpfs_dir_readdir,
    .ioctl = NULL,
};

static int devtmpfs_chrdev_open(file_t *file, const cred_t *cred)
{
	inode_t *inode = file->f_inode;

	if (!S_ISCHR(inode->i_mode))
		return -EINVAL;

	uint32_t major = MAJOR(inode->i_rdev);
	file_ops_t *drv_fops = chrdev_lookup(major);

	if (!drv_fops) {
		log("devtmpfs: open: no driver for major %u\n", major);
		return -ENXIO;
	}

	file->f_ops = drv_fops;

	if (drv_fops->open)
		return drv_fops->open(file, cred);

	return 0;
}

static void devtmpfs_chrdev_release(file_t *file)
{
	(void)file;
}

static int devtmpfs_chrdev_read(file_t *file, void *buf, size_t count)
{
	(void)file;
	(void)buf;
	(void)count;
	return -ENXIO;
}

static int devtmpfs_chrdev_write(file_t *file, const void *buf, size_t count)
{
	(void)file;
	(void)buf;
	(void)count;
	return -ENXIO;
}

static int devtmpfs_chrdev_ioctl(file_t *file, uint32_t cmd, void *arg)
{
	(void)file;
	(void)cmd;
	(void)arg;
	return -ENXIO;
}

static int devtmpfs_mount_cb(superblock_t *sb, const void *data)
{
	(void)data;
	tmpfs_sb_t *tsb = kmalloc(sizeof(tmpfs_sb_t));
	if (!tsb)
		return -ENOMEM;

	tsb->next_ino = 1;
	tsb->bytes_used = 0;
	tsb->bytes_limit = 0;

	sb->s_magic = DEVTMPFS_MAGIC;
	sb->s_block_size = 4096;
	sb->s_max_filename = VFS_NAME_MAX;
	sb->s_fs_info = tsb;
	sb->s_ops = &devtmpfs_sb_ops;

	inode_t *root = alloc_dev_inode(sb, S_IFDIR | 0755, 0, 0, 0);
	if (!root) {
		kfree(tsb);
		return -ENOMEM;
	}

	root->i_fops = &devtmpfs_dir_fops;

	tmpfs_inode_t *ti = (tmpfs_inode_t *)root->i_private;
	memcpy(ti->entries[0].name, ".", 2);
	ti->entries[0].inode = root;
	memcpy(ti->entries[1].name, "..", 3);
	ti->entries[1].inode = root;
	ti->entry_count = 2;

	root->i_nlink = 2;
	sb->s_root = root;

	log("devtmpfs: mounted (root ino=%llu)\n", root->i_ino);
	return 0;
}

static void devtmpfs_unmount_cb(superblock_t *sb)
{
	log("devtmpfs: unmounting\n");
	if (sb->s_root)
		free_inode_tree_dev(sb, sb->s_root);
	if (sb->s_fs_info) {
		kfree(sb->s_fs_info);
		sb->s_fs_info = NULL;
	}
	sb->s_root = NULL;
}

void devtmpfs_init(void)
{
	vfs_register_fs(&devtmpfs_fs_type);
	log("devtmpfs: registered\n");
}

static const cred_t _root_cred_dev = {.uid = 0, .gid = 0};

int devtmpfs_mknod(const char *name, uint32_t mode, uint32_t dev)
{
	if (!name)
		return -EINVAL;

	/* Build "/dev/<name>" */
	char path[VFS_PATH_MAX];
	size_t nlen = strlen(name);
	if (nlen + 6 >= VFS_PATH_MAX) /* "/dev/" = 5 chars + NUL */
		return -ENAMETOOLONG;

	memcpy(path, "/dev/", 5);
	memcpy(path + 5, name, nlen + 1);

	return vfs_mknod(path, mode, dev, &_root_cred_dev);
}