#include <fs/vfs.h>
#include <sys/errno.h>
#include <stdint.h>
#include <stddef.h>
#include <lib/string.h>
#include <sys/cred.h>
#include <sys/klog.h>
#include <mm/kheap.h>

static mount_t *mount_list = NULL;

superblock_t *vfs_mount(const char *path, superblock_t *sb)
{
	if (!path || !sb)
		return ERR_PTR(-EINVAL);

	mount_t *mnt = kmalloc(sizeof(mount_t));
	if (!mnt)
		return ERR_PTR(-ENOMEM);

	size_t plen = strlen(path) + 1;
	mnt->mnt_path = kmalloc(plen);
	if (!mnt->mnt_path) {
		kfree(mnt);
		return ERR_PTR(-ENOMEM);
	}

	memcpy(mnt->mnt_path, path, plen);
	mnt->mnt_sb = sb;
	mnt->mnt_root = sb->s_root;
	mnt->mnt_next = NULL;

	if (!mount_list)
		mount_list = mnt;
	else {
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
	if (!root || !path)
		return ERR_PTR(-EINVAL);

	inode_t *cur = root;
	const char *p = path;
	char component[VFS_NAME_MAX + 1];

	if (*p == '/') {
		while (*p == '/')
			p++;
		if (!*p)
			return cur;
	}

	while (*p) {
		size_t i = 0;

		while (*p && *p != '/' && i < VFS_NAME_MAX)
			component[i++] = *p++;

		component[i] = '\0';

		if (i == 0) {
			while (*p == '/')
				p++;
			continue;
		}

		if (!cur->i_ops || !cur->i_ops->lookup)
			return ERR_PTR(-ENOTDIR);

		cur = cur->i_ops->lookup(cur, component);
		if (!cur)
			return ERR_PTR(-ENOENT);

		while (*p == '/')
			p++;
	}

	return cur;
}

inode_t *vfs_lookup(inode_t *parent, const char *name)
{
	if (!parent || !name || !parent->i_ops || !parent->i_ops->lookup)
		return ERR_PTR(-EINVAL);

	return parent->i_ops->lookup(parent, name);
}

int vfs_mkdir(inode_t *parent, const char *name, mode_t mode)
{
	if (!parent || !name || !parent->i_ops || !parent->i_ops->mkdir)
		return -EINVAL;

	if (vfs_permission(parent, MAY_WRITE | MAY_EXEC) != 0)
		return -EACCES;

	return parent->i_ops->mkdir(parent, name, mode);
}

int vfs_mkdir_p(inode_t *root, const char *path, mode_t mode)
{
	inode_t *cur, *existing;
	char buf[VFS_NAME_MAX + 1];

	if (!root || !path)
		return -EINVAL;

	existing = vfs_resolve(root, path);
	if (!IS_ERR(existing) && existing)
		return S_ISDIR(existing->i_mode) ? 0 : -ENOTDIR;

	cur = root;

	while (*path == '/')
		path++;

	if (!*path)
		return 0;

	while (*path) {
		size_t i = 0;

		while (*path && *path != '/' && i < VFS_NAME_MAX)
			buf[i++] = *path++;

		buf[i] = '\0';

		if (i == 0) {
			while (*path == '/')
				path++;
			continue;
		}

		if (cur->i_ops && cur->i_ops->lookup) {
			existing = cur->i_ops->lookup(cur, buf);
			if (existing) {
				cur = existing;
				while (*path == '/')
					path++;
				continue;
			}
		}

		if (!cur->i_ops || !cur->i_ops->mkdir)
			return -EPERM;

		int err = cur->i_ops->mkdir(cur, buf, S_IFDIR | mode);
		if (err)
			return err;

		cur = cur->i_ops->lookup(cur, buf);
		if (!cur)
			return -EFAULT;

		while (*path == '/')
			path++;
	}

	return 0;
}

int vfs_create(inode_t *parent, const char *name, mode_t mode, inode_t **out)
{
	if (!parent || !name || !parent->i_ops || !parent->i_ops->create)
		return -EINVAL;

	if (vfs_permission(parent, MAY_WRITE | MAY_EXEC) != 0)
		return -EACCES;

	return parent->i_ops->create(parent, name, mode, out);
}

int vfs_mknod(inode_t *parent, const char *name, mode_t mode, dev_t rdev,
	      inode_t **out)
{
	if (!parent || !name || !parent->i_ops || !parent->i_ops->mknod)
		return -EINVAL;

	if (!cred_is_root())
		return -EPERM;

	return parent->i_ops->mknod(parent, name, mode, rdev, out);
}

static inode_t *vfs_path_resolve(const char *path)
{
	if (!path)
		return ERR_PTR(-EINVAL);

	mount_t *best = NULL;
	size_t best_len = 0;

	for (mount_t *m = mount_list; m; m = m->mnt_next) {
		size_t mlen = strlen(m->mnt_path);

		if (mlen < best_len)
			continue;

		if (strcmp(m->mnt_path, "/") == 0) {
			if (mlen > best_len) {
				best = m;
				best_len = mlen;
			}
		} else if (strncmp(path, m->mnt_path, mlen) == 0 &&
			   (path[mlen] == '/' || path[mlen] == '\0')) {
			if (mlen > best_len) {
				best = m;
				best_len = mlen;
			}
		}
	}

	if (!best)
		return ERR_PTR(-ENOENT);

	const char *rel = path + best_len;

	while (*rel == '/')
		rel++;

	inode_t *inode = vfs_resolve(best->mnt_root, rel);

	if (!inode)
		return ERR_PTR(-ENOENT);

	return inode;
}

inode_t *vfs_inode(const char *path)
{
	if (!path || !mount_list)
		return ERR_PTR(-EINVAL);

	return vfs_path_resolve(path);
}

file_t *vfs_open(const char *path, int flags)
{
	if (!path || !mount_list)
		return ERR_PTR(-EINVAL);

	inode_t *inode = vfs_path_resolve(path);
	if (IS_ERR(inode))
		return (file_t *)inode;

	int mask;

	switch (flags & 3) {
	case O_RDONLY:
		mask = MAY_READ;
		break;
	case O_WRONLY:
		mask = MAY_WRITE;
		break;
	case O_RDWR:
		mask = MAY_READ | MAY_WRITE;
		break;
	default:
		mask = MAY_READ;
		break;
	}

	if (vfs_permission(inode, mask) != 0)
		return ERR_PTR(-EACCES);

	file_t *file = kmalloc(sizeof(file_t));
	if (!file)
		return ERR_PTR(-ENOMEM);

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
	if (!file || IS_ERR(file))
		return;

	if (file->f_op && file->f_op->release)
		file->f_op->release(file->f_inode, file);

	kfree(file);
}

ssize_t vfs_read(file_t *file, void *buf, size_t count)
{
	if (!file || !buf || IS_ERR(file) || !file->f_op || !file->f_op->read)
		return -EINVAL;

	return file->f_op->read(file, buf, count, &file->f_pos);
}

ssize_t vfs_write(file_t *file, const void *buf, size_t count)
{
	if (!file || !buf || IS_ERR(file) || !file->f_op || !file->f_op->write)
		return -EINVAL;

	return file->f_op->write(file, buf, count, &file->f_pos);
}

int vfs_readdir(file_t *file, dirent_t *dirent)
{
	if (!file || !dirent || IS_ERR(file) || !file->f_op ||
	    !file->f_op->readdir)
		return -EINVAL;

	return file->f_op->readdir(file, dirent, &file->f_pos);
}

loff_t vfs_llseek(file_t *file, loff_t offset, int whence)
{
	if (!file || IS_ERR(file) || !file->f_op || !file->f_op->llseek)
		return -EINVAL;

	return file->f_op->llseek(file, offset, whence);
}

static int inode_getattr(inode_t *inode, stat_t *st)
{
	if (!inode || !st)
		return -EINVAL;

	if (inode->i_ops && inode->i_ops->getattr)
		return inode->i_ops->getattr(inode, st);

	st->st_ino = inode->i_ino;
	st->st_mode = inode->i_mode;
	st->st_uid = inode->i_uid;
	st->st_gid = inode->i_gid;
	st->st_nlink = inode->i_nlink;
	st->st_size = inode->i_size;
	st->st_rdev = inode->i_rdev;

	return 0;
}

int vfs_stat(const char *path, stat_t *st)
{
	if (!path || !st || !mount_list)
		return -EINVAL;

	inode_t *inode = vfs_path_resolve(path);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	return inode_getattr(inode, st);
}

int vfs_fstat(file_t *file, stat_t *st)
{
	if (!file || !st || IS_ERR(file))
		return -EINVAL;

	return inode_getattr(file->f_inode, st);
}

int vfs_chmod(const char *path, mode_t mode)
{
	if (!path || !mount_list)
		return -EINVAL;

	inode_t *inode = vfs_path_resolve(path);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	if (!cred_is_root() && current_cred->euid != inode->i_uid)
		return -EPERM;

	inode->i_mode = (inode->i_mode & S_IFMT) | (mode & ~S_IFMT);
	return 0;
}

int vfs_chown(const char *path, uid_t uid, gid_t gid)
{
	if (!path || !mount_list)
		return -EINVAL;

	inode_t *inode = vfs_path_resolve(path);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	if (!cred_is_root())
		return -EPERM;

	inode->i_uid = uid;
	inode->i_gid = gid;

	return 0;
}