#ifndef SYS_CRED_H
#define SYS_CRED_H

#include <sys/types.h>
#include <sys/errno.h>

#define CRED_NGROUPS_MAX 32

#define MAY_EXEC 1
#define MAY_WRITE 2
#define MAY_READ 4

typedef struct cred
{
    uid_t uid;
    gid_t gid;
    uid_t euid;
    gid_t egid;

    unsigned int ngroups;
    gid_t groups[CRED_NGROUPS_MAX];
} cred_t;

extern cred_t kernel_cred;
#define current_cred (&kernel_cred)

void cred_init(void);
struct inode;
int vfs_permission(const struct inode *inode, int mask);

static inline int cred_is_root(void)
{
    return current_cred->euid == 0;
}

#endif /* SYS_CRED_H */