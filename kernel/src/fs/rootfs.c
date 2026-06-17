#include <fs/rootfs.h>
#include <fs/cpio.h>
#include <lib/printk.h>
#include <lib/string.h>
#include <sys/panic.h>
#include <sys/conf.h>
#include <boot/limine.h>
#include <mm/pfn.h>

extern volatile struct limine_module_request mod_request;

static void cpio_handle(struct limine_file *mod)
{
	log("fs: cpio archive at %s (%llu bytes)\n", mod->path, mod->size);
	cpio_parse((void *)mod->address, (size_t)mod->size);
}

static void ramfs_handle(void)
{
	struct limine_file *ramfs_mod = NULL;

	for (uint64_t i = 0; i < mod_request.response->module_count; i++) {
		struct limine_file *mod = mod_request.response->modules[i];
		log("boot: discovered boot module: %s\n", mod->path);

		if (strncmp(mod->path, "ramfs.", 6) == 0 || strstr(mod->path, "/ramfs.") != NULL) {
			ramfs_mod = mod;
			break;
		}
	}

	if (!ramfs_mod)
		panic(NULL, "no ramfs module present");

	const char *p = ramfs_mod->path;
	size_t len = strlen(p);

	/* inline archive type table */
	struct {
		const char *ext;
		void (*handler)(struct limine_file *);
	} archive_table[] = {
	    {".cpio", cpio_handle},
	};

	for (size_t i = 0; i < sizeof(archive_table) / sizeof(archive_table[0]); i++) {
		size_t extlen = strlen(archive_table[i].ext);
		if (len >= extlen && strcmp(p + len - extlen, archive_table[i].ext) == 0) {
			archive_table[i].handler(ramfs_mod);
			return;
		}
	}

	panic(NULL, "unknown ramfs type: %s", p);
}

static void default_handle(void)
{
	panic(NULL, "unknown rootfs: %s", kernel_conf.rootfs);
}

struct rootfs_type {
	const char *name;
	void (*handler)(void);
};

static struct rootfs_type rootfs_table[] = {
    {"ramfs", ramfs_handle},
    {"default", default_handle},
};

void rootfs_init(void)
{
	if (kernel_conf.rootfs == NULL)
		panic(NULL, "no rootfs supplied, edit kernel cmdline to like: \"rootfs=ramfs\"");

	log("fs: using rootfs=%s\n", kernel_conf.rootfs);

	for (size_t i = 0; i < sizeof(rootfs_table) / sizeof(rootfs_table[0]); i++) {
		if (strcmp(rootfs_table[i].name, "default") == 0)
			continue;
		if (strcmp(kernel_conf.rootfs, rootfs_table[i].name) == 0) {
			rootfs_table[i].handler();
			return;
		}
	}

	default_handle();
}
