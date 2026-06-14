#include <sys/conf.h>
#include <lib/string.h>
#include <stddef.h>
#include <stdbool.h>

#define CONF_STR_ARENA_SIZE 1024

static char conf_str_arena[CONF_STR_ARENA_SIZE];
static size_t conf_str_off = 0;

struct kernel_conf kernel_conf = {
    .quiet = false,
    .rootfs = NULL,
};

static struct conf_entry conf_table[] = {
    {CONF_BOOL, "quiet", &kernel_conf.quiet},
    {CONF_STR, "rootfs", &kernel_conf.rootfs},
};

static int conf_bool_from_str(const char *val)
{
	return val && (strcasecmp(val, "yes") == 0 || strcasecmp(val, "true") == 0 ||
	               strcasecmp(val, "on") == 0 || strcmp(val, "1") == 0);
}

static char *conf_strdup_arena(const char *s)
{
	size_t len;
	char *dst;

	if (!s)
		return NULL;

	len = strlen(s) + 1;

	if (conf_str_off + len > CONF_STR_ARENA_SIZE)
		return NULL;

	dst = &conf_str_arena[conf_str_off];
	conf_str_off += len;

	memcpy(dst, s, len);

	return dst;
}

static char *conf_store_string(const char *val)
{
	size_t len;

	if (!val)
		return NULL;

	len = strlen(val);

	if (len >= 2 && val[0] == '"' && val[len - 1] == '"') {
		char tmp[128];

		if (len - 1 >= sizeof(tmp))
			return NULL;

		memcpy(tmp, val + 1, len - 2);
		tmp[len - 2] = '\0';

		return conf_strdup_arena(tmp);
	}

	return conf_strdup_arena(val);
}

void conf_parse(const char *str)
{
	char key[64], val[128];
	size_t i, j;

	if (!str)
		return;

	if (strcmp(str, "-1") == 0)
		return;

	while (*str) {
		while (*str && isspace(*str))
			str++;

		if (!*str)
			break;

		i = 0;
		while (*str && *str != '=' && !isspace(*str) && i < sizeof(key) - 1)
			key[i++] = *str++;
		key[i] = '\0';

		val[0] = '\0';
		if (*str == '=') {
			str++;
			j = 0;

			if (*str == '"') {
				str++;

				while (*str && *str != '"' && j < sizeof(val) - 1)
					val[j++] = *str++;

				if (*str == '"')
					str++;
			} else {
				while (*str && !isspace(*str) && j < sizeof(val) - 1)
					val[j++] = *str++;
			}

			val[j] = '\0';
		}

		for (i = 0; i < sizeof(conf_table) / sizeof(conf_table[0]); i++) {
			if (strcmp(key, conf_table[i].key) != 0)
				continue;

			switch (conf_table[i].type) {
			case CONF_BOOL:
				*(bool *)conf_table[i].field = conf_bool_from_str(val);
				break;

			case CONF_STR:
				*(char **)conf_table[i].field = conf_store_string(val);
				break;
			}
		}
	}
}