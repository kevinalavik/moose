#include <sys/conf.h>
#include <lib/string.h>

struct kernel_conf kernel_conf = {.kconsole = false};

static struct conf_entry conf_table[] = {
	{CONF_BOOL, "kconsole", &kernel_conf.kconsole},
};

static int conf_bool_from_str(const char *val)
{
	return strcasecmp(val, "yes") == 0 || strcasecmp(val, "true") == 0 ||
	       strcasecmp(val, "on") == 0;
}

void conf_parse(const char *str)
{
	char key[64], val[64];
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
		while (*str && !isspace(*str) && *str != '=' && i < 63)
			key[i++] = *str++;
		key[i] = '\0';

		if (*str == '=') {
			str++;
			j = 0;
			while (*str && !isspace(*str) && j < 63)
				val[j++] = *str++;
			val[j] = '\0';
		} else {
			val[0] = '\0';
		}

		for (i = 0; i < sizeof(conf_table) / sizeof(conf_table[0]); i++) {
			if (strcmp(key, conf_table[i].key) != 0)
				continue;
			switch (conf_table[i].type) {
			case CONF_BOOL:
				*(bool *)conf_table[i].field = conf_bool_from_str(val);
				break;
			}
		}
	}
}