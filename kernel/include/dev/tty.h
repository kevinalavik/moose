#ifndef DEV_TTY_H
#define DEV_TTY_H

#include <stddef.h>
#include <stdint.h>
#include <term/ansi.h>

#define TTY_NAME_MAX 16

#include <dev/dev.h>

typedef enum
{
    TTY_CTRL_PRESENT,
} tty_ctrl_cmd_t;

struct tty_ops
{
    size_t (*write)(void *priv, const void *buf, size_t len);
    size_t (*read)(void *priv, void *buf, size_t len);
    int (*control)(void *priv, tty_ctrl_cmd_t cmd, void *arg);
};

handle_t tty_register(const char *name, const struct tty_ops *ops, void *priv);
int tty_control(handle_t *h, tty_ctrl_cmd_t cmd, void *arg);

struct limine_framebuffer;
handle_t console_init(struct limine_framebuffer *fb,
                      const void *psf_data, size_t psf_size);

#endif
