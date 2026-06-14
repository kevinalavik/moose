#ifndef SYS_CONF_H
#define SYS_CONF_H

#include <stdbool.h>
#include <stddef.h>

enum conf_type {
	CONF_BOOL,
};

struct conf_entry {
	enum conf_type type;
	const char *key;
	void *field;
};

struct kernel_conf {
	bool kconsole;
};

extern struct kernel_conf kernel_conf;

void conf_parse(const char *conf);

#endif // SYS_CONF_H