#include <tty/tty.h>
#include <dev/device.h>
#include <dev/platform.h>
#include <term/term.h>

struct console {
	term_t term;
	bool active;
};

static size_t con_write(void *priv, const void *buf, size_t len)
{
	struct console *con = priv;
	if (!con->active)
		return len;
	for (size_t i = 0; i < len; i++)
		term_putc(&con->term, ((const char *)buf)[i]);
	return len;
}

static size_t con_read(void *priv, void *buf, size_t len)
{
	(void)priv;
	(void)buf;
	(void)len;
	return 0;
}

static int con_control(void *priv, tty_ctrl_cmd_t cmd, void *arg)
{
	struct console *con = priv;
	(void)arg;
	switch (cmd) {
	case TTY_CTRL_PRESENT:
		con->active = true;
		return 0;
	}
	return -1;
}

static const tty_ops_t con_ops = {
	.write = con_write,
	.read = con_read,
	.control = con_control,
};

int console_init(struct limine_framebuffer *fb, const void *psf_data,
		 size_t psf_size, device_t *dev)
{
	if (!fb || !psf_data || !dev)
		return -1;

	static struct console con;
	term_init(&con.term, fb, psf_data, psf_size);
	con.active = true;
	con.term.fb = fb;

	dev->chardev = tty_register("tty0", &con_ops, &con);
	return char_dev_valid(&dev->chardev) ? 0 : -1;
}
