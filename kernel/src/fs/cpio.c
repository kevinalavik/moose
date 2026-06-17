#include <fs/cpio.h>
#include <fs/vfs.h>
#include <lib/string.h>
#include <lib/printk.h>
#include <mm/heap.h>
#include <sys/errno.h>

#define CPIO_NEWC_MAGIC "070701"
#define CPIO_HEADER_LEN 110

struct cpio_newc_header {
	char c_magic[6];
	char c_ino[8];
	char c_mode[8];
	char c_uid[8];
	char c_gid[8];
	char c_nlink[8];
	char c_mtime[8];
	char c_filesize[8];
	char c_devmajor[8];
	char c_devminor[8];
	char c_rdevmajor[8];
	char c_rdevminor[8];
	char c_namesize[8];
	char c_check[8];
};

static unsigned long hex8(const char *s)
{
	unsigned long v = 0;
	for (int i = 0; i < 8; i++) {
		v <<= 4;
		char c = s[i];
		if (c >= '0' && c <= '9')
			v |= (unsigned long)(c - '0');
		else if (c >= 'a' && c <= 'f')
			v |= (unsigned long)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F')
			v |= (unsigned long)(c - 'A' + 10);
	}
	return v;
}

static void ensure_parent_dirs(const char *rel, const cred_t *cred)
{
	char dirpath[VFS_PATH_MAX];
	size_t dlen = 0;

	for (const char *s = rel; *s; s++) {
		if (*s == '/') {
			if (dlen > 0) {
				dirpath[0] = '/';
				memcpy(dirpath + 1, rel, dlen);
				dirpath[dlen + 1] = '\0';
				int err = 0;
				if (!vfs_resolve(dirpath, cred, &err) && err == -ENOENT)
					vfs_mkdir(dirpath, 0755, cred);
			}
		}
		dlen = (size_t)(s - rel) + 1;
	}
}

void cpio_parse(void *addr, size_t size)
{
	cred_t root_cred = {.uid = 0, .gid = 0};
	uint8_t *p = (uint8_t *)addr;
	uint8_t *end = p + size;

	(void)size;

	while (p + CPIO_HEADER_LEN <= end) {
		struct cpio_newc_header *hdr = (struct cpio_newc_header *)p;

		if (memcmp(hdr->c_magic, CPIO_NEWC_MAGIC, 6) != 0) {
			log("cpio: bad magic, stopping\n");
			return;
		}

		unsigned long namesize = hex8(hdr->c_namesize);
		unsigned long filesize = hex8(hdr->c_filesize);
		unsigned long mode = hex8(hdr->c_mode);
		unsigned long data_off = (CPIO_HEADER_LEN + namesize + 3) & ~3;

		char *name = (char *)(p + CPIO_HEADER_LEN);
		if (namesize >= 10 && memcmp(name, "TRAILER!!!", 10) == 0)
			break;

		/* skip "." entry */
		if (namesize == 2 && name[0] == '.' && name[1] == '\0')
			goto next_entry;
		if (namesize == 1 && name[0] == '.')
			goto next_entry;

		const char *rel = name;
		if (rel[0] == '.' && rel[1] == '/')
			rel += 2;

		size_t rel_len = strlen(rel);
		while (rel_len > 0 && rel[rel_len - 1] == '/')
			rel_len--;

		if (rel_len == 0)
			goto next_entry;

		char vfs_path[VFS_PATH_MAX];
		if (rel_len + 1 >= VFS_PATH_MAX)
			goto next_entry;
		vfs_path[0] = '/';
		memcpy(vfs_path + 1, rel, rel_len);
		vfs_path[rel_len + 1] = '\0';

		ensure_parent_dirs(rel, &root_cred);

		if (S_ISDIR(mode)) {
			int err = 0;
			if (!vfs_resolve(vfs_path, &root_cred, &err) && err == -ENOENT) {
				vfs_mkdir(vfs_path, (uint32_t)mode, &root_cred);
			}
		} else if (S_ISREG(mode)) {
			int err = 0;
			file_t *f = vfs_open(vfs_path,
			                     O_WRONLY | O_CREAT | O_TRUNC,
			                     (uint32_t)mode,
			                     &root_cred,
			                     &err);
			if (f) {
				if (filesize > 0) {
					void *data = p + data_off;
					vfs_write(f, data, (size_t)filesize);
				}
				vfs_close(f);
				log("cpio: %s (%lu bytes)\n", vfs_path, filesize);
			} else {
				log("cpio: failed to create %s: %d\n", vfs_path, err);
			}
		} else {
			log("cpio: skipping %s (mode 0%lo)\n", vfs_path, mode);
		}

	next_entry:;
		unsigned long entry_size = (data_off + filesize + 3) & ~3;
		p += entry_size;
	}

	log("cpio: done parsing archive\n");
}
