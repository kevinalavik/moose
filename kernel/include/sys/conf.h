#ifndef SYS_CONF_H
#define SYS_CONF_H

#include <stdbool.h>
#include <stddef.h>

enum conf_type {
	CONF_BOOL,
	CONF_STR,
};

struct conf_entry {
	enum conf_type type;
	const char *key;
	void *field;
};

struct kernel_conf {
	bool quiet;
	char *rootfs;
};

extern struct kernel_conf kernel_conf;

void conf_parse(const char *conf);

#endif // SYS_CONF_H