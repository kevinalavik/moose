#include <dev/tty.h>
#include <dev/chrdev.h>
#include <fs/devtmpfs.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/printk.h>
#include <sys/errno.h>
#include <sys/spinlock.h>
#include <flanterm.h>
#include <flanterm_backends/fb.h>
#include <extra/ascii_font.h>

static tty_t tty_table[TTY_MAX];
static int tty_active_idx = -1; /* -1 = not yet initialised     */
static spinlock_t tty_global_lock;

static void *tty_ft_malloc(size_t size)
{
	return kmalloc(size);
}

static void tty_ft_free(void *ptr, size_t size)
{
	(void)size;
	kfree(ptr);
}

static inline size_t ibuf_next(size_t idx)
{
	return (idx + 1) % TTY_IBUF_SIZE;
}

static inline bool ibuf_empty(const tty_t *tty)
{
	return tty->ibuf_head == tty->ibuf_tail;
}

static inline bool ibuf_full(const tty_t *tty)
{
	return ibuf_next(tty->ibuf_head) == tty->ibuf_tail;
}

static void tty_putbyte(tty_t *tty, char c);

void tty_input_byte(tty_t *tty, char c)
{
	unsigned long flags = spin_lock_irqsave(&tty->lock);

	/* ICRNL: map \r -> \n */
	if ((tty->termios.c_iflag & ICRNL) && c == '\r')
		c = '\n';
	/* INLCR: map \n -> \r */
	else if ((tty->termios.c_iflag & INLCR) && c == '\n')
		c = '\r';
	/* IGNCR: discard \r */
	else if ((tty->termios.c_iflag & IGNCR) && c == '\r') {
		spin_unlock_irqrestore(&tty->lock, flags);
		return;
	}

	if (!ibuf_full(tty)) {
		tty->ibuf[tty->ibuf_head] = c;
		tty->ibuf_head = ibuf_next(tty->ibuf_head);
	}

	/* local echo when ECHO is set */
	if (tty->termios.c_lflag & ECHO) {
		if ((tty->termios.c_lflag & ECHOE) && c == 0x08) {
			tty_putbyte(tty, 0x08);
			tty_putbyte(tty, ' ');
			tty_putbyte(tty, 0x08);
		} else {
			tty_putbyte(tty, c);
		}
	}

	/* if full, character is silently dropped (no scroll back) */
	spin_unlock_irqrestore(&tty->lock, flags);
}

static void tty_putbyte(tty_t *tty, char c)
{
	if (!tty->ft_ctx)
		return;

	if (tty->termios.c_oflag & OPOST) {
		/* ONLCR: translate \n -> \r\n */
		if ((tty->termios.c_oflag & ONLCR) && c == '\n') {
			char cr = '\r';
			flanterm_write(tty->ft_ctx, &cr, 1);
		}
		/* OCRNL: translate \r -> \n */
		else if ((tty->termios.c_oflag & OCRNL) && c == '\r') {
			c = '\n';
		}
	}

	flanterm_write(tty->ft_ctx, &c, 1);
}

static int tty_fops_open(file_t *file, const cred_t *cred)
{
	(void)cred;

	inode_t *inode = file->f_inode;
	uint32_t minor = MINOR(inode->i_rdev);

	if (minor >= TTY_MAX || !tty_table[minor].initialised)
		return -ENXIO;

	tty_t *tty = &tty_table[minor];

	unsigned long flags = spin_lock_irqsave(&tty->lock);
	tty->open_count++;
	file->f_private = tty;
	spin_unlock_irqrestore(&tty->lock, flags);
	return 0;
}

static void tty_fops_release(file_t *file)
{
	tty_t *tty = (tty_t *)file->f_private;
	if (!tty)
		return;

	unsigned long flags = spin_lock_irqsave(&tty->lock);
	if (tty->open_count > 0)
		tty->open_count--;
	spin_unlock_irqrestore(&tty->lock, flags);
}

static int tty_fops_read(file_t *file, void *buf, size_t count)
{
	tty_t *tty = (tty_t *)file->f_private;
	if (!tty)
		return -ENXIO;
	if (!buf || count == 0)
		return 0;

	char *dst = (char *)buf;
	size_t n = 0;

	/*
     * TODO: replace with a proper wait queue once the scheduler exists.
     */
	while (n == 0) {
		unsigned long flags = spin_lock_irqsave(&tty->lock);
		while (n < count && !ibuf_empty(tty)) {
			dst[n++] = tty->ibuf[tty->ibuf_tail];
			tty->ibuf_tail = ibuf_next(tty->ibuf_tail);
		}
		spin_unlock_irqrestore(&tty->lock, flags);

		if (n == 0) {
			__asm__ volatile("pause" ::: "memory");
		}
	}

	return (int)n;
}

static int tty_fops_write(file_t *file, const void *buf, size_t count)
{
	tty_t *tty = (tty_t *)file->f_private;
	if (!tty)
		return -ENXIO;
	if (!buf || count == 0)
		return 0;

	const char *src = (const char *)buf;

	unsigned long flags = spin_lock_irqsave(&tty->lock);
	for (size_t i = 0; i < count; i++)
		tty_putbyte(tty, src[i]);
	spin_unlock_irqrestore(&tty->lock, flags);

	return (int)count;
}

static int64_t tty_fops_seek(file_t *file, int64_t offset, int whence)
{
	(void)file;
	(void)offset;
	(void)whence;
	return -ESPIPE;
}

static int tty_fops_ioctl(file_t *file, uint32_t cmd, void *arg)
{
	tty_t *tty = (tty_t *)file->f_private;
	if (!tty)
		return -ENXIO;

	switch (cmd) {
	case TIOCGWINSZ: {
		if (!arg)
			return -EFAULT;
		tty_winsize_t *ws = (tty_winsize_t *)arg;
		unsigned long flags = spin_lock_irqsave(&tty->lock);
		if (tty->ft_ctx) {
			size_t cols, rows;
			flanterm_get_dimensions(tty->ft_ctx, &cols, &rows);
			ws->ws_col = (uint16_t)cols;
			ws->ws_row = (uint16_t)rows;
		} else {
			ws->ws_col = 80;
			ws->ws_row = 25;
		}
		ws->ws_xpixel = 0;
		ws->ws_ypixel = 0;
		spin_unlock_irqrestore(&tty->lock, flags);
		return 0;
	}

	case TIOCSWINSZ:
		/* We don't support dynamic resize yet; silently succeed. */
		return 0;

	case TCGETS: {
		if (!arg)
			return -EFAULT;
		tty_termios_t *t = (tty_termios_t *)arg;
		unsigned long flags = spin_lock_irqsave(&tty->lock);
		*t = tty->termios;
		spin_unlock_irqrestore(&tty->lock, flags);
		return 0;
	}

	case TCSETS: {
		if (!arg)
			return -EFAULT;
		const tty_termios_t *t = (const tty_termios_t *)arg;
		unsigned long flags = spin_lock_irqsave(&tty->lock);
		tty->termios = *t;
		spin_unlock_irqrestore(&tty->lock, flags);
		return 0;
	}

	default:
		return -ENOTTY;
	}
}

static file_ops_t tty_fops = {
    .open = tty_fops_open,
    .release = tty_fops_release,
    .read = tty_fops_read,
    .write = tty_fops_write,
    .seek = tty_fops_seek,
    .readdir = NULL,
    .ioctl = tty_fops_ioctl,
};

tty_t *tty_get_active(void)
{
	int idx = tty_active_idx;
	if (idx < 0 || idx >= TTY_MAX)
		return NULL;
	if (!tty_table[idx].initialised)
		return NULL;
	return &tty_table[idx];
}

int tty_switch(int n)
{
	if (n < 0 || n >= TTY_MAX)
		return -EINVAL;

	unsigned long flags = spin_lock_irqsave(&tty_global_lock);

	if (!tty_table[n].initialised) {
		spin_unlock_irqrestore(&tty_global_lock, flags);
		return -EINVAL;
	}

	tty_active_idx = n;
	spin_unlock_irqrestore(&tty_global_lock, flags);

	if (tty_table[n].ft_ctx)
		flanterm_full_refresh(tty_table[n].ft_ctx);

	log("tty: switched to tty%d\n", n);
	return 0;
}

void tty_init(void *fb_addr,
              size_t fb_width,
              size_t fb_height,
              size_t fb_pitch,
              uint8_t red_mask_size,
              uint8_t red_mask_shift,
              uint8_t green_mask_size,
              uint8_t green_mask_shift,
              uint8_t blue_mask_size,
              uint8_t blue_mask_shift)
{
	spin_init(&tty_global_lock);

	for (int i = 0; i < TTY_MAX; i++) {
		tty_t *t = &tty_table[i];
		memset(t, 0, sizeof(*t));
		spin_init(&t->lock);
		t->index = i;

		/* Default termios: output processing on, NL->CR+NL, canonical */
		t->termios.c_iflag = ICRNL;
		t->termios.c_oflag = OPOST | ONLCR;
		t->termios.c_lflag = ICANON;
	}

	tty_t *tty0 = &tty_table[0];
	if (fb_addr) {
		tty0->ft_ctx = flanterm_fb_init(tty_ft_malloc,
		                                tty_ft_free,
		                                (uint32_t *)fb_addr,
		                                fb_width,
		                                fb_height,
		                                fb_pitch,
		                                red_mask_size,
		                                red_mask_shift,
		                                green_mask_size,
		                                green_mask_shift,
		                                blue_mask_size,
		                                blue_mask_shift,
		                                NULL,
		                                NULL,
		                                NULL,
		                                NULL,
		                                NULL,
		                                NULL,
		                                NULL,
		                                (void *)ascii_font,
		                                ASCII_FONT_COUNT,
		                                ASCII_FONT_WIDTH,
		                                ASCII_FONT_HEIGHT,
		                                0,
		                                0,
		                                0,
		                                0,
		                                FLANTERM_FB_ROTATE_0);

		if (!tty0->ft_ctx) {
			log("tty: WARN: flanterm_fb_init failed for tty0, no display output\n");
		} else {
			size_t cols, rows;
			flanterm_get_dimensions(tty0->ft_ctx, &cols, &rows);
			log("tty: tty0 flanterm context: %llux%llu chars\n",
			    (unsigned long long)cols,
			    (unsigned long long)rows);
		}
	} else {
		log("tty: no framebuffer; tty0 will have no display output\n");
	}

	tty0->initialised = true;
	tty_active_idx = 0;

	int ret = chrdev_register(TTY_MAJOR, "tty", &tty_fops);
	if (ret < 0) {
		log("tty: WARN: chrdev_register failed: %d\n", ret);
	}

	ret = devtmpfs_mknod("tty0", S_IFCHR | 0620, MKDEV(TTY_MAJOR, 0));
	if (ret < 0) {
		log("tty: WARN: devtmpfs_mknod tty0 failed: %d\n", ret);
	} else {
		log("tty: created /dev/tty0 (major=%d minor=0)\n", TTY_MAJOR);
	}

	log("tty: initialised (%d ttys available, tty0 active)\n", TTY_MAX);
}