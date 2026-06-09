#include <tty/tty.h>
#include <lib/string.h>

struct tty_instance {
	tty_ops_t ops;
	void *priv;
	char name[TTY_NAME_MAX];
};

static struct tty_instance instances[8];
static uint32_t instance_count;

static size_t tty_dev_read(char_dev_t *d, void *buf, size_t len)
{
	struct tty_instance *ti = d->private_data;
	return ti->ops.read(ti->priv, buf, len);
}

static size_t tty_dev_write(char_dev_t *d, const void *buf, size_t len)
{
	struct tty_instance *ti = d->private_data;
	return ti->ops.write(ti->priv, buf, len);
}

char_dev_t tty_register(const char *name, const tty_ops_t *ops, void *priv)
{
	if (!name || !ops || !priv)
		return CHAR_DEV_NULL;

	if (instance_count >= 8)
		return CHAR_DEV_NULL;

	struct tty_instance *ti = &instances[instance_count++];
	ti->ops = *ops;
	ti->priv = priv;

	size_t n = strlen(name);
	if (n >= TTY_NAME_MAX)
		n = TTY_NAME_MAX - 1;
	memcpy(ti->name, name, n);
	ti->name[n] = '\0';

	return (char_dev_t){
		.private_data = ti,
		.read = tty_dev_read,
		.write = tty_dev_write,
		.name = ti->name,
	};
}
