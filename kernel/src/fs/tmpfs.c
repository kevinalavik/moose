#include <fs/tmpfs.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/printk.h>
#include <sys/panic.h>
#include <sys/errno.h>

/* big ugly forward decl, to not flud tmpfs.h */

static inode_t *tmpfs_alloc_inode(superblock_t *sb);
static void tmpfs_destroy_inode(superblock_t *sb, inode_t *inode);
static int tmpfs_write_inode(superblock_t *sb, inode_t *inode);
static int tmpfs_sync_fs(superblock_t *sb);
static inode_t *tmpfs_lookup(inode_t *dir, const char *name, const cred_t *cred, int *err);
static inode_t *
tmpfs_create(inode_t *dir, const char *name, uint32_t mode, const cred_t *cred, int *err);
static inode_t *
tmpfs_mkdir_op(inode_t *dir, const char *name, uint32_t mode, const cred_t *cred, int *err);
static int tmpfs_rmdir_op(inode_t *dir, const char *name, const cred_t *cred);
static int tmpfs_unlink(inode_t *dir, const char *name, const cred_t *cred);
static int tmpfs_rename(inode_t *old_dir,
                        const char *old_name,
                        inode_t *new_dir,
                        const char *new_name,
                        const cred_t *cred);
static int tmpfs_link(inode_t *dir, const char *name, inode_t *target, const cred_t *cred);
static inode_t *
tmpfs_symlink_op(inode_t *dir, const char *name, const char *target, const cred_t *cred, int *err);
static int tmpfs_readlink(inode_t *inode, char *buf, size_t bufsz);
static int tmpfs_truncate(inode_t *inode, uint64_t size, const cred_t *cred);
static int tmpfs_getattr(inode_t *inode);
static int
tmpfs_setattr(inode_t *inode, uint32_t mode, uint32_t uid, uint32_t gid, const cred_t *cred);
static int tmpfs_file_open(file_t *file, const cred_t *cred);
static void tmpfs_file_release(file_t *file);
static int tmpfs_file_read(file_t *file, void *buf, size_t count);
static int tmpfs_file_write(file_t *file, const void *buf, size_t count);
static int64_t tmpfs_file_seek(file_t *file, int64_t offset, int whence);
static int tmpfs_dir_open(file_t *file, const cred_t *cred);
static void tmpfs_dir_release(file_t *file);
static int tmpfs_dir_readdir(file_t *file, int (*emit)(void *ctx, const dirent_t *de), void *ctx);
static int tmpfs_mount_cb(superblock_t *sb, const void *data);
static void tmpfs_unmount_cb(superblock_t *sb);

static superblock_ops_t tmpfs_sb_ops = {
    .alloc_inode = tmpfs_alloc_inode,
    .destroy_inode = tmpfs_destroy_inode,
    .write_inode = tmpfs_write_inode,
    .sync_fs = tmpfs_sync_fs,
};

static inode_ops_t tmpfs_inode_ops = {
    .lookup = tmpfs_lookup,
    .create = tmpfs_create,
    .mkdir = tmpfs_mkdir_op,
    .rmdir = tmpfs_rmdir_op,
    .unlink = tmpfs_unlink,
    .rename = tmpfs_rename,
    .link = tmpfs_link,
    .symlink = tmpfs_symlink_op,
    .readlink = tmpfs_readlink,
    .truncate = tmpfs_truncate,
    .getattr = tmpfs_getattr,
    .setattr = tmpfs_setattr,
};

static file_ops_t tmpfs_file_fops = {
    .open = tmpfs_file_open,
    .release = tmpfs_file_release,
    .read = tmpfs_file_read,
    .write = tmpfs_file_write,
    .seek = tmpfs_file_seek,
    .readdir = NULL,
    .ioctl = NULL,
};

static file_ops_t tmpfs_dir_fops = {
    .open = tmpfs_dir_open,
    .release = tmpfs_dir_release,
    .read = NULL,
    .write = NULL,
    .seek = NULL,
    .readdir = tmpfs_dir_readdir,
    .ioctl = NULL,
};

static fs_type_t tmpfs_fs_type = {
    .name = "tmpfs",
    .mount = tmpfs_mount_cb,
    .unmount = tmpfs_unmount_cb,
    .next = NULL,
};

static inode_t *alloc_raw_inode(superblock_t *sb, uint32_t mode, uint32_t uid, uint32_t gid)
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
	inode->i_sb = sb;
	inode->i_private = ti;
	inode->i_ops = &tmpfs_inode_ops;

	/* asign the right file_ops based on type */
	if (S_ISDIR(mode))
		inode->i_fops = &tmpfs_dir_fops;
	else
		inode->i_fops = &tmpfs_file_fops;

	return inode;
}

static int dir_add_entry(inode_t *dir, const char *name, inode_t *child)
{
	tmpfs_inode_t *tdir = (tmpfs_inode_t *)dir->i_private;

	if (tdir->entry_count >= TMPFS_MAX_DIRENTS) {
		log("tmpfs: dir_add_entry: directory full (inode %llu)\n", dir->i_ino);
		return -ENOSPC;
	}

	size_t namelen = strlen(name);
	if (namelen > VFS_NAME_MAX)
		return -ENAMETOOLONG;

	tmpfs_dirent_t *de = &tdir->entries[tdir->entry_count];
	memcpy(de->name, name, namelen + 1);
	de->inode = child;
	tdir->entry_count++;

	dir->i_mtime = dir->i_ctime = 0; /* TODO: real timestamps */
	return 0;
}

static int dir_remove_entry(inode_t *dir, const char *name)
{
	tmpfs_inode_t *tdir = (tmpfs_inode_t *)dir->i_private;

	for (size_t i = 0; i < tdir->entry_count; i++) {
		if (strcmp(tdir->entries[i].name, name) == 0) {
			for (size_t j = i; j + 1 < tdir->entry_count; j++)
				tdir->entries[j] = tdir->entries[j + 1];
			tdir->entry_count--;
			return 0;
		}
	}
	return -ENOENT;
}

static inode_t *dir_find_entry(inode_t *dir, const char *name)
{
	tmpfs_inode_t *tdir = (tmpfs_inode_t *)dir->i_private;

	for (size_t i = 0; i < tdir->entry_count; i++) {
		if (strcmp(tdir->entries[i].name, name) == 0)
			return tdir->entries[i].inode;
	}
	return NULL;
}

static void free_inode_tree(superblock_t *sb, inode_t *inode)
{
	if (!inode)
		return;

	tmpfs_inode_t *ti = (tmpfs_inode_t *)inode->i_private;

	if (S_ISDIR(inode->i_mode)) {
		for (size_t i = 0; i < ti->entry_count; i++) {
			const char *n = ti->entries[i].name;
			/* skip "." and ".." to avoid infinite loops, found that out :^) */
			if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0)
				continue;
			free_inode_tree(sb, ti->entries[i].inode);
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

static inode_t *tmpfs_alloc_inode(superblock_t *sb)
{
	/* generi inode alloc, actual filling of the inode os done by create/mkdir */
	return alloc_raw_inode(sb, 0, 0, 0);
}

static void tmpfs_destroy_inode(superblock_t *sb, inode_t *inode)
{
	(void)sb;
	if (!inode)
		return;

	tmpfs_inode_t *ti = (tmpfs_inode_t *)inode->i_private;
	if (ti) {
		if (S_ISREG(inode->i_mode) && ti->data)
			kfree(ti->data);
		if (S_ISLNK(inode->i_mode) && ti->link_target)
			kfree(ti->link_target);
		kfree(ti);
	}
	kfree(inode);
}

static int tmpfs_write_inode(superblock_t *sb, inode_t *inode)
{
	(void)sb;
	(void)inode;
	return 0;
}

static int tmpfs_sync_fs(superblock_t *sb)
{
	(void)sb;
	return 0;
}

static inode_t *tmpfs_lookup(inode_t *dir, const char *name, const cred_t *cred, int *err)
{
	(void)cred;

	if (!S_ISDIR(dir->i_mode)) {
		if (err)
			*err = -ENOTDIR;
		return NULL;
	}

	inode_t *child = dir_find_entry(dir, name);
	if (!child) {
		if (err)
			*err = -ENOENT;
		return NULL;
	}

	return child;
}

static inode_t *
tmpfs_create(inode_t *dir, const char *name, uint32_t mode, const cred_t *cred, int *err)
{
	if (!S_ISDIR(dir->i_mode)) {
		if (err)
			*err = -ENOTDIR;
		return NULL;
	}

	if (dir_find_entry(dir, name)) {
		if (err)
			*err = -EEXIST;
		return NULL;
	}

	inode_t *inode =
	    alloc_raw_inode(dir->i_sb, (mode & ~S_IFMT) | S_IFREG, cred->uid, cred->gid);
	if (!inode) {
		if (err)
			*err = -ENOMEM;
		return NULL;
	}

	int ret = dir_add_entry(dir, name, inode);
	if (ret < 0) {
		tmpfs_destroy_inode(dir->i_sb, inode);
		if (err)
			*err = ret;
		return NULL;
	}

	// log("tmpfs: created file '%s' (ino %llu)\n", name, inode->i_ino); more old debug
	return inode;
}

static inode_t *
tmpfs_mkdir_op(inode_t *dir, const char *name, uint32_t mode, const cred_t *cred, int *err)
{
	if (!S_ISDIR(dir->i_mode)) {
		if (err)
			*err = -ENOTDIR;
		return NULL;
	}

	if (dir_find_entry(dir, name)) {
		if (err)
			*err = -EEXIST;
		return NULL;
	}

	inode_t *inode =
	    alloc_raw_inode(dir->i_sb, (mode & ~S_IFMT) | S_IFDIR, cred->uid, cred->gid);
	if (!inode) {
		if (err)
			*err = -ENOMEM;
		return NULL;
	}

	/* "." and ".." entries */
	tmpfs_inode_t *ti = (tmpfs_inode_t *)inode->i_private;
	memcpy(ti->entries[0].name, ".", 2);
	ti->entries[0].inode = inode;
	memcpy(ti->entries[1].name, "..", 3);
	ti->entries[1].inode = dir;
	ti->entry_count = 2;

	inode->i_nlink = 2; /* one from parent + . */
	dir->i_nlink++;     /* .. in the new dir points back */

	int ret = dir_add_entry(dir, name, inode);
	if (ret < 0) {
		tmpfs_destroy_inode(dir->i_sb, inode);
		if (err)
			*err = ret;
		return NULL;
	}

	// log("tmpfs: created dir '%s' (ino %llu)\n", name, inode->i_ino);
	return inode;
}

static int tmpfs_rmdir_op(inode_t *dir, const char *name, const cred_t *cred)
{
	(void)cred;

	inode_t *child = dir_find_entry(dir, name);
	if (!child)
		return -ENOENT;

	if (!S_ISDIR(child->i_mode))
		return -ENOTDIR;

	tmpfs_inode_t *tci = (tmpfs_inode_t *)child->i_private;
	/* only "." and ".." means empty */
	if (tci->entry_count > 2)
		return -ENOTEMPTY;

	dir_remove_entry(dir, name);
	dir->i_nlink--;

	tmpfs_destroy_inode(dir->i_sb, child);
	return 0;
}

static int tmpfs_unlink(inode_t *dir, const char *name, const cred_t *cred)
{
	(void)cred;

	inode_t *child = dir_find_entry(dir, name);
	if (!child)
		return -ENOENT;

	if (S_ISDIR(child->i_mode))
		return -EISDIR;

	dir_remove_entry(dir, name);
	child->i_nlink--;

	if (child->i_nlink == 0)
		tmpfs_destroy_inode(dir->i_sb, child);

	return 0;
}

static int tmpfs_rename(inode_t *old_dir,
                        const char *old_name,
                        inode_t *new_dir,
                        const char *new_name,
                        const cred_t *cred)
{
	(void)cred;

	inode_t *src = dir_find_entry(old_dir, old_name);
	if (!src)
		return -ENOENT;

	inode_t *dst = dir_find_entry(new_dir, new_name);
	if (dst) {
		if (S_ISDIR(dst->i_mode)) {
			tmpfs_inode_t *tdst = (tmpfs_inode_t *)dst->i_private;
			if (tdst->entry_count > 2)
				return -ENOTEMPTY;
		}
		dir_remove_entry(new_dir, new_name);
		dst->i_nlink--;
		if (dst->i_nlink == 0)
			tmpfs_destroy_inode(new_dir->i_sb, dst);
	}

	int ret = dir_add_entry(new_dir, new_name, src);
	if (ret < 0)
		return ret;

	dir_remove_entry(old_dir, old_name);
	return 0;
}

static int tmpfs_link(inode_t *dir, const char *name, inode_t *target, const cred_t *cred)
{
	(void)cred;

	if (S_ISDIR(target->i_mode))
		return -EPERM; /* hard links to directories not allowed */

	if (dir_find_entry(dir, name))
		return -EEXIST;

	int ret = dir_add_entry(dir, name, target);
	if (ret < 0)
		return ret;

	target->i_nlink++;
	return 0;
}

static inode_t *
tmpfs_symlink_op(inode_t *dir, const char *name, const char *target, const cred_t *cred, int *err)
{
	if (dir_find_entry(dir, name)) {
		if (err)
			*err = -EEXIST;
		return NULL;
	}

	inode_t *inode = alloc_raw_inode(dir->i_sb, S_IFLNK | 0777, cred->uid, cred->gid);
	if (!inode) {
		if (err)
			*err = -ENOMEM;
		return NULL;
	}

	tmpfs_inode_t *ti = (tmpfs_inode_t *)inode->i_private;
	ti->link_target = strdup(target);
	if (!ti->link_target) {
		tmpfs_destroy_inode(dir->i_sb, inode);
		if (err)
			*err = -ENOMEM;
		return NULL;
	}

	inode->i_size = strlen(target);

	int ret = dir_add_entry(dir, name, inode);
	if (ret < 0) {
		tmpfs_destroy_inode(dir->i_sb, inode);
		if (err)
			*err = ret;
		return NULL;
	}

	return inode;
}

static int tmpfs_readlink(inode_t *inode, char *buf, size_t bufsz)
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

static int tmpfs_truncate(inode_t *inode, uint64_t size, const cred_t *cred)
{
	(void)cred;

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
		uint8_t *newdata = kmalloc((size_t)size);
		if (!newdata)
			return -ENOMEM;

		if (ti->data) {
			memcpy(newdata, ti->data, ti->data_size);
			kfree(ti->data);
		}
		memset(newdata + ti->data_size, 0, (size_t)(size - ti->data_size));

		tsb->bytes_used += (size - ti->data_size);
		ti->data = newdata;
		ti->data_cap = (size_t)size;
	} else if (size < ti->data_size) {
		tsb->bytes_used -= (ti->data_size - size);
	}

	ti->data_size = (size_t)size;
	inode->i_size = size;
	inode->i_blocks = (uint32_t)((size + 511) / 512);
	return 0;
}

static int tmpfs_getattr(inode_t *inode)
{
	/* everything is always up-to-date in RAM */
	(void)inode;
	return 0;
}

static int
tmpfs_setattr(inode_t *inode, uint32_t mode, uint32_t uid, uint32_t gid, const cred_t *cred)
{
	/* only root or owner may change attributes */
	if (cred->uid != 0 && cred->uid != inode->i_uid)
		return -EPERM;

	inode->i_mode = (inode->i_mode & S_IFMT) | (mode & ~S_IFMT);
	inode->i_uid = uid;
	inode->i_gid = gid;
	return 0;
}

static int tmpfs_file_open(file_t *file, const cred_t *cred)
{
	(void)cred;
	/* noop: data lives in inode->i_private */
	(void)file;
	return 0;
}

static void tmpfs_file_release(file_t *file)
{
	/* nothing allocated in the actual file_t so we noop here again */
	(void)file;
}

static int tmpfs_file_read(file_t *file, void *buf, size_t count)
{
	inode_t *inode = file->f_inode;
	tmpfs_inode_t *ti = (tmpfs_inode_t *)inode->i_private;

	if (file->f_pos >= inode->i_size)
		return 0; /* EOF */

	uint64_t remaining = inode->i_size - file->f_pos;
	if ((uint64_t)count > remaining)
		count = (size_t)remaining;

	memcpy(buf, ti->data + file->f_pos, count);
	file->f_pos += count;
	return (int)count;
}

static int tmpfs_file_write(file_t *file, const void *buf, size_t count)
{
	if (count == 0)
		return 0;

	inode_t *inode = file->f_inode;
	tmpfs_inode_t *ti = (tmpfs_inode_t *)inode->i_private;
	tmpfs_sb_t *tsb = (tmpfs_sb_t *)inode->i_sb->s_fs_info;

	/* O_APPEND: always write at end */
	if (file->f_flags & O_APPEND)
		file->f_pos = inode->i_size;

	uint64_t end = file->f_pos + count;

	/* Grow the data buffer if needed */
	if (end > ti->data_cap) {
		/* Double capacity at least up to end */
		size_t new_cap = ti->data_cap ? ti->data_cap * 2 : 64;
		while (new_cap < (size_t)end)
			new_cap *= 2;

		uint8_t *newdata = kmalloc(new_cap);
		if (!newdata)
			return -ENOMEM;

		if (ti->data) {
			memcpy(newdata, ti->data, ti->data_size);
			kfree(ti->data);
		}

		/* zero gap between old size and write start (sparse writes) */
		if (file->f_pos > ti->data_size)
			memset(newdata + ti->data_size, 0, (size_t)(file->f_pos - ti->data_size));

		ti->data = newdata;
		ti->data_cap = new_cap;
	}

	/* if we are writing past current logical size, zero the gap */
	if (file->f_pos > ti->data_size)
		memset(ti->data + ti->data_size, 0, (size_t)(file->f_pos - ti->data_size));

	memcpy(ti->data + file->f_pos, buf, count);

	/* update logical size */
	if (end > ti->data_size) {
		tsb->bytes_used += (end - ti->data_size);
		ti->data_size = (size_t)end;
		inode->i_size = end;
		inode->i_blocks = (uint32_t)((end + 511) / 512);
	}

	file->f_pos += count;
	return (int)count;
}

static int64_t tmpfs_file_seek(file_t *file, int64_t offset, int whence)
{
	int64_t new_pos;

	switch (whence) {
	case SEEK_SET:
		new_pos = offset;
		break;
	case SEEK_CUR:
		new_pos = (int64_t)file->f_pos + offset;
		break;
	case SEEK_END:
		new_pos = (int64_t)file->f_inode->i_size + offset;
		break;
	default:
		return -EINVAL;
	}

	if (new_pos < 0)
		return -EINVAL;

	file->f_pos = (uint64_t)new_pos;
	return new_pos;
}

static int tmpfs_dir_open(file_t *file, const cred_t *cred)
{
	(void)file;
	(void)cred;
	return 0;
}

static void tmpfs_dir_release(file_t *file)
{
	(void)file;
}

static int tmpfs_dir_readdir(file_t *file, int (*emit)(void *ctx, const dirent_t *de), void *ctx)
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

		uint32_t mode = tde->inode->i_mode;
		if (S_ISREG(mode))
			de.d_type = DT_REG;
		else if (S_ISDIR(mode))
			de.d_type = DT_DIR;
		else if (S_ISLNK(mode))
			de.d_type = DT_LNK;
		else if (S_ISCHR(mode))
			de.d_type = DT_CHR;
		else if (S_ISBLK(mode))
			de.d_type = DT_BLK;
		else if (S_ISFIFO(mode))
			de.d_type = DT_FIFO;
		else if (S_ISSOCK(mode))
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

static int tmpfs_mount_cb(superblock_t *sb, const void *data)
{
	(void)data;

	tmpfs_sb_t *tsb = kmalloc(sizeof(tmpfs_sb_t));
	if (!tsb)
		return -ENOMEM;

	tsb->next_ino = 1;
	tsb->bytes_used = 0;
	tsb->bytes_limit = 0; /* unlimited */

	sb->s_magic = TMPFS_MAGIC;
	sb->s_block_size = 4096;
	sb->s_max_filename = VFS_NAME_MAX;
	sb->s_fs_info = tsb;
	sb->s_ops = &tmpfs_sb_ops;

	/* make the root inode (inode 1) */
	inode_t *root = alloc_raw_inode(sb, S_IFDIR | 0755, 0, 0);
	if (!root) {
		kfree(tsb);
		return -ENOMEM;
	}

	tmpfs_inode_t *ti = (tmpfs_inode_t *)root->i_private;
	memcpy(ti->entries[0].name, ".", 2);
	ti->entries[0].inode = root;
	memcpy(ti->entries[1].name, "..", 3);
	ti->entries[1].inode = root;
	ti->entry_count = 2;

	root->i_nlink = 2;
	sb->s_root = root;

	log("tmpfs: mounted (root ino=%llu)\n", root->i_ino);
	return 0;
}

static void tmpfs_unmount_cb(superblock_t *sb)
{
	log("tmpfs: unmounting\n");
	if (sb->s_root)
		free_inode_tree(sb, sb->s_root);

	if (sb->s_fs_info) {
		kfree(sb->s_fs_info);
		sb->s_fs_info = NULL;
	}

	sb->s_root = NULL;
}

void tmpfs_init(void)
{
	vfs_register_fs(&tmpfs_fs_type);
	log("tmpfs: registered\n");
}