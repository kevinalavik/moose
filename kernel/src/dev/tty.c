#include <dev/tty.h>
#include <lib/string.h>

struct tty_instance {
	tty_ops_t ops;
	void *priv;
	char name[TTY_NAME_MAX];
};

static size_t tty_dev_write(struct handle *h, const void *buf, size_t len)
{
	struct tty_instance *ti = h->dev;
	return ti->ops.write(ti->priv, buf, len);
}

static size_t tty_dev_read(struct handle *h, void *buf, size_t len)
{
	struct tty_instance *ti = h->dev;
	return ti->ops.read(ti->priv, buf, len);
}

static const dev_ops_t tty_dev_ops = {
	.read = tty_dev_read,
	.write = tty_dev_write,
};

handle_t tty_register(const char *name, const struct tty_ops *ops, void *priv)
{
	if (!name || !ops || !priv)
		return HANDLE_INVALID;

	static struct tty_instance instances[8];
	static uint32_t count;

	if (count >= 8)
		return HANDLE_INVALID;

	struct tty_instance *ti = &instances[count++];
	ti->ops = *ops;
	ti->priv = priv;

	size_t n = strlen(name);
	if (n >= TTY_NAME_MAX)
		n = TTY_NAME_MAX - 1;
	memcpy(ti->name, name, n);
	ti->name[n] = '\0';

	return device_handle_make(ti, &tty_dev_ops, ti->name);
}

int tty_control(handle_t *h, tty_ctrl_cmd_t cmd, void *arg)
{
	if (!h || !h->dev)
		return -1;

	struct tty_instance *ti = h->dev;
	return ti->ops.control(ti->priv, cmd, arg);
}
