#include <sys/cred.h>
#include <fs/vfs.h>
#include <sys/klog.h>

cred_t kernel_cred;

void cred_init(void)
{
	kernel_cred.uid = 0;
	kernel_cred.gid = 0;
	kernel_cred.euid = 0;
	kernel_cred.egid = 0;
	kernel_cred.ngroups = 0;
	klog("cred", "initialized: uid=%u gid=%u (root)", kernel_cred.uid,
	     kernel_cred.gid);
}

static int cred_in_group(const cred_t *cr, gid_t gid)
{
	if (cr->egid == gid)
		return 1;
	for (unsigned int i = 0; i < cr->ngroups; i++)
		if (cr->groups[i] == gid)
			return 1;
	return 0;
}

int vfs_permission(const inode_t *inode, int mask)
{
	const cred_t *cr = current_cred;
	mode_t mode = inode->i_mode;
	mode_t check;

	if (cr->euid == 0)
		return 0;

	if (cr->euid == inode->i_uid)
		check = (mode >> 6) & 7; /* owner bits */
	else if (cred_in_group(cr, inode->i_gid))
		check = (mode >> 3) & 7; /* group bits */
	else
		check = mode & 7; /* other bits */

	if ((check & mask) == (unsigned)mask)
		return 0;

	return -EACCES;
}