#include <fs/devfs.h>
#include <fs/tmpfs.h>
#include <fs/vfs.h>
#include <dev/dev.h>
#include <sys/cred.h>
#include <sys/klog.h>
#include <mm/kheap.h>
#include <lib/string.h>
#include <sys/types.h>

struct devfs_node {
	handle_t handle;
	char name[VFS_NAME_MAX + 1];
};

static int devfs_chr_open(inode_t *inode, file_t *file);
static int devfs_chr_release(inode_t *inode, file_t *file);
static ssize_t devfs_chr_read(file_t *file, void *buf, size_t count,
			      loff_t *pos);
static ssize_t devfs_chr_write(file_t *file, const void *buf, size_t count,
			       loff_t *pos);
static int devfs_chr_getattr(inode_t *inode, stat_t *st);

static const file_ops_t devfs_chr_fops = {
	.open = devfs_chr_open,
	.release = devfs_chr_release,
	.read = devfs_chr_read,
	.write = devfs_chr_write,
};

static const inode_ops_t devfs_chr_iops = {
	.getattr = devfs_chr_getattr,
};

static superblock_t *devfs_sb = NULL;
static inode_t *devfs_root = NULL;

static int devfs_chr_open(inode_t *inode, file_t *file)
{
	(void)inode;
	(void)file;
	return 0;
}

static int devfs_chr_release(inode_t *inode, file_t *file)
{
	(void)inode;
	(void)file;
	return 0;
}

static ssize_t devfs_chr_read(file_t *file, void *buf, size_t count,
			      loff_t *pos)
{
	struct devfs_node *dn = file->f_inode->i_private;
	(void)pos;
	if (!dn || !device_handle_valid(&dn->handle))
		return -1;
	return (ssize_t)device_read(&dn->handle, buf, count);
}

static ssize_t devfs_chr_write(file_t *file, const void *buf, size_t count,
			       loff_t *pos)
{
	struct devfs_node *dn = file->f_inode->i_private;
	(void)pos;
	if (!dn || !device_handle_valid(&dn->handle))
		return -1;
	return (ssize_t)device_write(&dn->handle, buf, count);
}

static int devfs_chr_getattr(inode_t *inode, stat_t *st)
{
	st->st_ino = inode->i_ino;
	st->st_mode = inode->i_mode;
	st->st_uid = inode->i_uid;
	st->st_gid = inode->i_gid;
	st->st_nlink = inode->i_nlink;
	st->st_size = 0;
	st->st_rdev = inode->i_rdev;
	return 0;
}

static inode_t *devfs_alloc_chr_inode(handle_t *handle, dev_t rdev)
{
	struct devfs_node *dn = kmalloc(sizeof(struct devfs_node));
	if (!dn)
		return NULL;

	dn->handle = *handle;

	inode_t *inode = kmalloc(sizeof(inode_t));
	if (!inode) {
		kfree(dn);
		return NULL;
	}

	inode->i_ino = devfs_sb->s_ino_next++;
	inode->i_mode = S_IFCHR | 0660;
	inode->i_uid = 0;
	inode->i_gid = 0;
	inode->i_size = 0;
	inode->i_nlink = 1;
	inode->i_rdev = rdev;
	inode->i_sb = devfs_sb;
	inode->i_ops = &devfs_chr_iops;
	inode->i_fop = &devfs_chr_fops;
	inode->i_parent = devfs_root;
	inode->i_private = dn;
	return inode;
}

superblock_t *devfs_mount(void)
{
	superblock_t *sb = tmpfs_mount();
	if (!sb)
		return NULL;
	devfs_sb = sb;
	devfs_root = sb->s_root;
	return sb;
}

void devfs_init(void)
{
	superblock_t *sb = devfs_mount();
	if (!sb) {
		klog("devfs", "failed to mount");
		return;
	}
	vfs_mount("/dev", sb);
	klog("devfs", "initialized at /dev");
}

int devfs_register(const char *name, handle_t *handle)
{
	if (!name || !handle || !devfs_root)
		return -1;

	if (!device_handle_valid(handle)) {
		klog("devfs", "register: invalid handle for '%s'", name);
		return -1;
	}

	if (vfs_lookup(devfs_root, name)) {
		klog("devfs", "register: '%s' already exists", name);
		return -1;
	}

	inode_t *inode = devfs_alloc_chr_inode(handle, 0);
	if (!inode) {
		klog("devfs", "register: OOM for '%s'", name);
		return -1;
	}

	inode_t *stub = NULL;
	int err = vfs_mknod(devfs_root, name, S_IFCHR | 0660, 0, &stub);
	if (err || !stub) {
		kfree(inode->i_private);
		kfree(inode);
		klog("devfs", "register: mknod failed for '%s'", name);
		return -1;
	}

	struct devfs_node *dn = inode->i_private;
	size_t nlen = strlen(name);
	if (nlen > VFS_NAME_MAX)
		nlen = VFS_NAME_MAX;
	memcpy(dn->name, name, nlen);
	dn->name[nlen] = '\0';
	stub->i_ops = &devfs_chr_iops;
	stub->i_fop = &devfs_chr_fops;
	stub->i_private = dn;
	stub->i_mode = S_IFCHR | 0660;
	kfree(inode);
	klog("devfs", "registered /dev/%s", name);
	return 0;
}

int devfs_unregister(const char *name)
{
	if (!name || !devfs_root)
		return -1;

	inode_t *inode = vfs_lookup(devfs_root, name);
	if (!inode) {
		klog("devfs", "unregister: '%s' not found", name);
		return -1;
	}

	if (inode->i_private) {
		kfree(inode->i_private);
		inode->i_private = NULL;
	}
	inode->i_fop = NULL;
	inode->i_ops = NULL;
	klog("devfs", "unregistered /dev/%s", name);
	return 0;
}