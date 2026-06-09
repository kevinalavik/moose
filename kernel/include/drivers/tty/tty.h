#ifndef DEV_TTY_H
#define DEV_TTY_H

#include <stddef.h>
#include <stdint.h>
#include <term/ansi.h>
#include <device/device.h>

#define TTY_NAME_MAX 16

typedef enum {
	TTY_CTRL_PRESENT,
} tty_ctrl_cmd_t;

typedef struct tty_ops {
	size_t (*write)(void *priv, const void *buf, size_t len);
	size_t (*read)(void *priv, void *buf, size_t len);
	int (*control)(void *priv, tty_ctrl_cmd_t cmd, void *arg);
} tty_ops_t;

char_dev_t tty_register(const char *name, const tty_ops_t *ops, void *priv);

struct limine_framebuffer;
int console_init(struct limine_framebuffer *fb, const void *psf_data,
		 size_t psf_size, device_t *dev);

#endif /* DEV_TTY_H */
