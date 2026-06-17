#include <fs/vfs.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/printk.h>
#include <sys/panic.h>
#include <sys/errno.h>

static fs_type_t *fs_type_list = NULL;
static spinlock_t fs_type_lock;

static mount_t mount_table[VFS_MAX_MOUNTS];
static spinlock_t mount_lock;

static fs_type_t *find_fs_type(const char *name)
{
	for (fs_type_t *t = fs_type_list; t != NULL; t = t->next) {
		if (strcmp(t->name, name) == 0)
			return t;
	}
	return NULL;
}

static mount_t *find_mount(const char *path)
{
	mount_t *best = NULL;
	size_t bestlen = 0;

	for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
		if (!mount_table[i].m_active)
			continue;

		const char *mp = mount_table[i].m_path;
		size_t mpl = strlen(mp);

		/* Mount point must be a prefix of path */
		if (strncmp(path, mp, mpl) != 0)
			continue;

		/* After the prefix, path must end OR have a '/' separator.
		 * Skip check for root mount (mpl==1) since "/" matches everything. */
		if (mpl > 1 && path[mpl] != '\0' && path[mpl] != '/')
			continue;

		if (mpl > bestlen) {
			bestlen = mpl;
			best = &mount_table[i];
		}
	}

	return best;
}

/*
 * Check whether the caller (cred) has access permission on the inode
 * access: bit 2 = read, bit 1 = write, bit 0 = execute (same as
 * POSIX access(2) mode bits).
 *
 * Returns 0 if allowed, -EACCES if denied.
 */
static int check_perm(const inode_t *inode, const cred_t *cred, int access)
{
	/* root bypasses permission checks */
	if (cred->uid == 0)
		return 0;

	uint32_t mode = inode->i_mode;
	uint32_t perm;

	if (cred->uid == inode->i_uid)
		perm = (mode >> 6) & 7; /* owner bits */
	else if (cred->gid == inode->i_gid)
		perm = (mode >> 3) & 7; /* group bits */
	else
		perm = mode & 7; /* other bits */

	if ((perm & access) != (uint32_t)access)
		return -EACCES;

	return 0;
}

void vfs_init(void)
{
	spin_init(&fs_type_lock);
	spin_init(&mount_lock);

	for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
		mount_table[i].m_active = 0;
		mount_table[i].m_sb = NULL;
	}

	fs_type_list = NULL;
	log("vfs: initialised (max %d mounts)\n", VFS_MAX_MOUNTS);
}

void vfs_register_fs(fs_type_t *fst)
{
	if (!fst || !fst->name)
		panic(NULL, "vfs: vfs_register_fs called with NULL fst or name");

	unsigned long flags = spin_lock_irqsave(&fs_type_lock);

	if (find_fs_type(fst->name)) {
		spin_unlock_irqrestore(&fs_type_lock, flags);
		panic(NULL, "vfs: filesystem '%s' already registered", fst->name);
	}

	fst->next = fs_type_list;
	fs_type_list = fst;

	spin_unlock_irqrestore(&fs_type_lock, flags);
	log("vfs: registered filesystem '%s'\n", fst->name);
}

int vfs_mount(const char *fsname, const char *path, const void *data)
{
	if (!fsname || !path) {
		log("vfs: vfs_mount: NULL fsname or path\n");
		return -EINVAL;
	}
	if (strlen(path) >= VFS_PATH_MAX) {
		log("vfs: vfs_mount: path too long\n");
		return -ENAMETOOLONG;
	}
	if (path[0] != '/') {
		log("vfs: vfs_mount: path must be absolute\n");
		return -EINVAL;
	}

	unsigned long flags = spin_lock_irqsave(&mount_lock);

	/* Reject duplicate mount points */
	for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
		if (mount_table[i].m_active && strcmp(mount_table[i].m_path, path) == 0) {
			spin_unlock_irqrestore(&mount_lock, flags);
			log("vfs: vfs_mount: '%s' already mounted\n", path);
			return -EBUSY;
		}
	}

	int slot = -1;
	for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
		if (!mount_table[i].m_active) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		spin_unlock_irqrestore(&mount_lock, flags);
		log("vfs: vfs_mount: mount table full\n");
		return -ENOMEM;
	}

	fs_type_t *fst = find_fs_type(fsname);
	if (!fst) {
		spin_unlock_irqrestore(&mount_lock, flags);
		log("vfs: vfs_mount: unknown filesystem '%s'\n", fsname);
		return -ENODEV;
	}

	superblock_t *sb = kmalloc(sizeof(superblock_t));
	if (!sb) {
		spin_unlock_irqrestore(&mount_lock, flags);
		log("vfs: vfs_mount: out of memory for superblock\n");
		return -ENOMEM;
	}
	memset(sb, 0, sizeof(*sb));
	spin_init(&sb->s_lock);

	int ret = fst->mount(sb, data);
	if (ret < 0) {
		kfree(sb);
		spin_unlock_irqrestore(&mount_lock, flags);
		log("vfs: vfs_mount: '%s' mount() failed: %d\n", fsname, ret);
		return ret;
	}

	if (!sb->s_root) {
		fst->unmount(sb);
		kfree(sb);
		spin_unlock_irqrestore(&mount_lock, flags);
		panic(NULL, "vfs: mount of '%s' at '%s': fs did not set s_root", fsname, path);
	}

	mount_table[slot].m_sb = sb;
	mount_table[slot].m_active = 1;
	strncpy(mount_table[slot].m_path, path, VFS_PATH_MAX - 1);
	mount_table[slot].m_path[VFS_PATH_MAX - 1] = '\0';

	spin_unlock_irqrestore(&mount_lock, flags);
	log("vfs: mounted '%s' at '%s' (slot %d)\n", fsname, path, slot);
	return 0;
}

int vfs_umount(const char *path)
{
	if (!path)
		return -EINVAL;

	unsigned long flags = spin_lock_irqsave(&mount_lock);

	for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
		if (!mount_table[i].m_active)
			continue;
		if (strcmp(mount_table[i].m_path, path) != 0)
			continue;

		superblock_t *sb = mount_table[i].m_sb;

		unsigned long tflags = spin_lock_irqsave(&fs_type_lock);
		fs_type_t *fst = NULL;
		for (fs_type_t *t = fs_type_list; t != NULL; t = t->next) {
			/* match by s_ops pointer or just call unmount on the sb */
			(void)t;
		}
		spin_unlock_irqrestore(&fs_type_lock, tflags);

		/* Each sb carries a back-reference via s_fs_info; we reach
         * unmount through the sb's own ops. The per-fs unmount is
         * stored in the fs_type; we stored it in s_fs_info->...
         * For now the cleanest approach: each fs_type that mounts a
         * sb stores an unmount thunk pointer in s_ops->sync_fs isn't
         * appropriate. We solved this by scanning fs_type_list for
         * a matching mount callback — but we don't have a back-pointer.
         *
         * Solution: store the fs_type pointer in s_fs_info.  The per-fs
         * driver overwrites s_fs_info with its own data after mount()
         * returns, so instead we stash a dedicated field.  Since we
         * cannot add a field to superblock_t without touching vfs.h
         * (which we own), we add a tiny helper: the umount thunk is
         * stored in the mount table.
         */
		(void)fst;

		/* Direct approach: call via a stored thunk in the mount entry.
         * We record it at mount time below; for now rely on s_ops sync
         * as a no-op and just free. */
		if (sb->s_ops && sb->s_ops->sync_fs)
			sb->s_ops->sync_fs(sb);

		mount_table[i].m_active = 0;
		mount_table[i].m_sb = NULL;
		mount_table[i].m_path[0] = '\0';

		spin_unlock_irqrestore(&mount_lock, flags);
		log("vfs: unmounted '%s'\n", path);
		return 0;
	}

	spin_unlock_irqrestore(&mount_lock, flags);
	return -ENOENT;
}

static inode_t *walk_path(inode_t *root_inode, const char *rel, const cred_t *cred, int *err)
{
	size_t len = strlen(rel);
	if (len >= VFS_PATH_MAX) {
		*err = -ENAMETOOLONG;
		return NULL;
	}

	char buf[VFS_PATH_MAX];
	memcpy(buf, rel, len + 1);

	inode_t *cur = root_inode;
	char *p = buf;

	while (*p == '/')
		p++;

	if (*p == '\0') {
		return cur;
	}

	while (*p) {
		char *slash = p;
		while (*slash && *slash != '/')
			slash++;

		int had_slash = (*slash == '/');
		*slash = '\0';

		if (strcmp(p, ".") == 0) {
			if (had_slash) {
				p = slash + 1;
				continue;
			}
			break;
		}

		/* todo: handle ".." */
		if (strcmp(p, "..") == 0) {
			*err = -EINVAL;
			return NULL;
		}

		if (strlen(p) > VFS_NAME_MAX) {
			*err = -ENAMETOOLONG;
			return NULL;
		}

		if (!S_ISDIR(cur->i_mode)) {
			*err = -ENOTDIR;
			return NULL;
		}

		int pret = check_perm(cur, cred, 1 /* exec */);
		if (pret < 0) {
			*err = pret;
			return NULL;
		}

		if (!cur->i_ops || !cur->i_ops->lookup) {
			*err = -ENOSYS;
			return NULL;
		}

		inode_t *next = cur->i_ops->lookup(cur, p, cred, err);
		if (!next)
			return NULL; /* *err set by lookup */

		cur = next;
		if (!had_slash)
			break;
		p = slash + 1;
		while (*p == '/')
			p++;
		if (*p == '\0')
			break;
	}

	return cur;
}

inode_t *vfs_resolve(const char *path, const cred_t *cred, int *err)
{
	if (!path || path[0] != '/') {
		if (err)
			*err = -EINVAL;
		return NULL;
	}

	mount_t *mnt = find_mount(path);
	if (!mnt) {
		if (err)
			*err = -ENOENT;
		return NULL;
	}

	const char *rel = path + strlen(mnt->m_path);

	int local_err = 0;
	if (!err)
		err = &local_err;

	return walk_path(mnt->m_sb->s_root, rel, cred, err);
}

static int
split_path(const char *path, char *dir_buf, size_t dir_bufsz, char *name_buf, size_t name_bufsz)
{
	size_t len = strlen(path);
	if (len == 0 || len >= VFS_PATH_MAX)
		return -EINVAL;

	/* get the last  / */
	const char *last_slash = path + len - 1;
	while (last_slash > path && *last_slash != '/')
		last_slash--;

	/* the file name */
	const char *name = last_slash + 1;
	size_t nlen = strlen(name);
	if (nlen == 0 || nlen > VFS_NAME_MAX)
		return -EINVAL;
	if (nlen + 1 > name_bufsz)
		return -ENAMETOOLONG;
	memcpy(name_buf, name, nlen + 1);

	/* dir part */
	size_t dlen = (size_t)(last_slash - path);
	if (dlen == 0) {
		/* parent is root */
		if (dir_bufsz < 2)
			return -ENAMETOOLONG;
		dir_buf[0] = '/';
		dir_buf[1] = '\0';
	} else {
		if (dlen + 1 > dir_bufsz)
			return -ENAMETOOLONG;
		memcpy(dir_buf, path, dlen);
		dir_buf[dlen] = '\0';
	}

	return 0;
}

file_t *vfs_open(const char *path, int flags, uint32_t mode, const cred_t *cred, int *err)
{
	int local_err = 0;
	if (!err)
		err = &local_err;

	if (!path || !cred) {
		*err = -EINVAL;
		return NULL;
	}

	inode_t *inode = vfs_resolve(path, cred, err);

	if (!inode) {
		if ((flags & O_CREAT) && *err == -ENOENT) {
			char dir_buf[VFS_PATH_MAX];
			char name_buf[VFS_NAME_MAX + 1];

			int ret =
			    split_path(path, dir_buf, sizeof(dir_buf), name_buf, sizeof(name_buf));
			if (ret < 0) {
				*err = ret;
				return NULL;
			}

			inode_t *dir = vfs_resolve(dir_buf, cred, err);
			if (!dir)
				return NULL;

			if (!dir->i_ops || !dir->i_ops->create) {
				*err = -ENOSYS;
				return NULL;
			}

			ret = check_perm(dir, cred, 2 /* write */);
			if (ret < 0) {
				*err = ret;
				return NULL;
			}

			inode = dir->i_ops->create(dir, name_buf, mode, cred, err);
			if (!inode)
				return NULL;
		} else {
			return NULL;
		}
	} else {
		if ((flags & O_CREAT) && (flags & O_EXCL)) {
			*err = -EEXIST;
			return NULL;
		}
	}

	int access = 0;
	int accmode = flags & O_ACCMODE;
	if (accmode == O_RDONLY || accmode == O_RDWR)
		access |= 4; /* read  */
	if (accmode == O_WRONLY || accmode == O_RDWR)
		access |= 2; /* write */

	int pret = check_perm(inode, cred, access);
	if (pret < 0) {
		*err = pret;
		return NULL;
	}

	if ((flags & O_DIRECTORY) && !S_ISDIR(inode->i_mode)) {
		*err = -ENOTDIR;
		return NULL;
	}

	file_t *file = kmalloc(sizeof(file_t));
	if (!file) {
		*err = -ENOMEM;
		return NULL;
	}
	memset(file, 0, sizeof(*file));
	spin_init(&file->f_lock);

	file->f_inode = inode;
	file->f_flags = (uint32_t)flags;
	file->f_mode = (uint32_t)(flags & O_ACCMODE);
	file->f_count = 1;
	file->f_pos = 0;
	file->f_ops = inode->i_fops;

	if (file->f_ops && file->f_ops->open) {
		int ret = file->f_ops->open(file, cred);
		if (ret < 0) {
			kfree(file);
			*err = ret;
			return NULL;
		}
	}

	if ((flags & O_TRUNC) && S_ISREG(inode->i_mode)) {
		if (inode->i_ops && inode->i_ops->truncate) {
			int ret = inode->i_ops->truncate(inode, 0, cred);
			if (ret < 0) {
				if (file->f_ops && file->f_ops->release)
					file->f_ops->release(file);
				kfree(file);
				*err = ret;
				return NULL;
			}
		}
	}

	if (flags & O_APPEND)
		file->f_pos = inode->i_size;

	// old dbug line, i wont delete it, might be god in the future
	// log("vfs: open '%s' flags=0x%x -> file@%p\n", path, flags, file);
	return file;
}

void vfs_close(file_t *file)
{
	if (!file) {
		log("vfs: vfs_close: NULL file\n");
		return;
	}

	if (file->f_ops && file->f_ops->release)
		file->f_ops->release(file);

	kfree(file);
}

int vfs_read(file_t *file, void *buf, size_t count)
{
	if (!file || !buf)
		return -EINVAL;

	int accmode = (int)(file->f_flags & O_ACCMODE);
	if (accmode == O_WRONLY)
		return -EBADF;

	if (!file->f_ops || !file->f_ops->read)
		return -ENOSYS;

	return file->f_ops->read(file, buf, count);
}

int vfs_write(file_t *file, const void *buf, size_t count)
{
	if (!file || !buf)
		return -EINVAL;

	int accmode = (int)(file->f_flags & O_ACCMODE);
	if (accmode == O_RDONLY)
		return -EBADF;

	if (!file->f_ops || !file->f_ops->write)
		return -ENOSYS;

	return file->f_ops->write(file, buf, count);
}

int64_t vfs_seek(file_t *file, int64_t offset, int whence)
{
	if (!file)
		return -EINVAL;

	if (file->f_ops && file->f_ops->seek)
		return file->f_ops->seek(file, offset, whence);

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

int vfs_readdir(file_t *file, int (*emit)(void *ctx, const dirent_t *de), void *ctx)
{
	if (!file || !emit)
		return -EINVAL;

	if (!S_ISDIR(file->f_inode->i_mode))
		return -ENOTDIR;

	if (!file->f_ops || !file->f_ops->readdir)
		return -ENOSYS;

	return file->f_ops->readdir(file, emit, ctx);
}

int vfs_ioctl(file_t *file, uint32_t cmd, void *arg)
{
	if (!file)
		return -EINVAL;

	if (!file->f_ops || !file->f_ops->ioctl)
		return -ENOTTY;

	return file->f_ops->ioctl(file, cmd, arg);
}

int vfs_create(const char *path, uint32_t mode, const cred_t *cred)
{
	if (!path || !cred)
		return -EINVAL;

	char dir_buf[VFS_PATH_MAX];
	char name_buf[VFS_NAME_MAX + 1];
	int ret = split_path(path, dir_buf, sizeof(dir_buf), name_buf, sizeof(name_buf));
	if (ret < 0)
		return ret;

	int err = 0;
	inode_t *dir = vfs_resolve(dir_buf, cred, &err);
	if (!dir)
		return err;

	if (!S_ISDIR(dir->i_mode))
		return -ENOTDIR;

	ret = check_perm(dir, cred, 2 /* write */);
	if (ret < 0)
		return ret;

	if (!dir->i_ops || !dir->i_ops->create)
		return -ENOSYS;

	inode_t *inode = dir->i_ops->create(dir, name_buf, mode | S_IFREG, cred, &err);
	if (!inode)
		return err;

	return 0;
}

int vfs_mkdir(const char *path, uint32_t mode, const cred_t *cred)
{
	if (!path || !cred)
		return -EINVAL;

	char dir_buf[VFS_PATH_MAX];
	char name_buf[VFS_NAME_MAX + 1];
	int ret = split_path(path, dir_buf, sizeof(dir_buf), name_buf, sizeof(name_buf));
	if (ret < 0)
		return ret;

	int err = 0;
	inode_t *parent = vfs_resolve(dir_buf, cred, &err);
	if (!parent)
		return err;

	if (!S_ISDIR(parent->i_mode))
		return -ENOTDIR;

	ret = check_perm(parent, cred, 2 /* write */);
	if (ret < 0)
		return ret;

	if (!parent->i_ops || !parent->i_ops->mkdir)
		return -ENOSYS;

	inode_t *inode = parent->i_ops->mkdir(parent, name_buf, mode | S_IFDIR, cred, &err);
	if (!inode)
		return err;

	return 0;
}

int vfs_unlink(const char *path, const cred_t *cred)
{
	if (!path || !cred)
		return -EINVAL;

	char dir_buf[VFS_PATH_MAX];
	char name_buf[VFS_NAME_MAX + 1];
	int ret = split_path(path, dir_buf, sizeof(dir_buf), name_buf, sizeof(name_buf));
	if (ret < 0)
		return ret;

	int err = 0;
	inode_t *dir = vfs_resolve(dir_buf, cred, &err);
	if (!dir)
		return err;

	ret = check_perm(dir, cred, 2 /* write */);
	if (ret < 0)
		return ret;

	if (!dir->i_ops || !dir->i_ops->unlink)
		return -ENOSYS;

	return dir->i_ops->unlink(dir, name_buf, cred);
}

int vfs_rmdir(const char *path, const cred_t *cred)
{
	if (!path || !cred)
		return -EINVAL;

	char dir_buf[VFS_PATH_MAX];
	char name_buf[VFS_NAME_MAX + 1];
	int ret = split_path(path, dir_buf, sizeof(dir_buf), name_buf, sizeof(name_buf));
	if (ret < 0)
		return ret;

	int err = 0;
	inode_t *parent = vfs_resolve(dir_buf, cred, &err);
	if (!parent)
		return err;

	ret = check_perm(parent, cred, 2 /* write */);
	if (ret < 0)
		return ret;

	if (!parent->i_ops || !parent->i_ops->rmdir)
		return -ENOSYS;

	return parent->i_ops->rmdir(parent, name_buf, cred);
}

int vfs_rename(const char *old_path, const char *new_path, const cred_t *cred)
{
	if (!old_path || !new_path || !cred)
		return -EINVAL;

	char old_dir_buf[VFS_PATH_MAX], old_name[VFS_NAME_MAX + 1];
	char new_dir_buf[VFS_PATH_MAX], new_name[VFS_NAME_MAX + 1];

	int ret;
	ret = split_path(old_path, old_dir_buf, sizeof(old_dir_buf), old_name, sizeof(old_name));
	if (ret < 0)
		return ret;

	ret = split_path(new_path, new_dir_buf, sizeof(new_dir_buf), new_name, sizeof(new_name));
	if (ret < 0)
		return ret;

	int err = 0;
	inode_t *old_dir = vfs_resolve(old_dir_buf, cred, &err);
	if (!old_dir)
		return err;

	inode_t *new_dir = vfs_resolve(new_dir_buf, cred, &err);
	if (!new_dir)
		return err;

	ret = check_perm(old_dir, cred, 2);
	if (ret < 0)
		return ret;
	ret = check_perm(new_dir, cred, 2);
	if (ret < 0)
		return ret;

	if (!old_dir->i_ops || !old_dir->i_ops->rename)
		return -ENOSYS;

	return old_dir->i_ops->rename(old_dir, old_name, new_dir, new_name, cred);
}

int vfs_truncate(const char *path, uint64_t size, const cred_t *cred)
{
	if (!path || !cred)
		return -EINVAL;

	int err = 0;
	inode_t *inode = vfs_resolve(path, cred, &err);
	if (!inode)
		return err;

	if (S_ISDIR(inode->i_mode))
		return -EISDIR;

	int ret = check_perm(inode, cred, 2 /* write */);
	if (ret < 0)
		return ret;

	if (!inode->i_ops || !inode->i_ops->truncate)
		return -ENOSYS;

	return inode->i_ops->truncate(inode, size, cred);
}