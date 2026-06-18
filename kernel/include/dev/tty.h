#ifndef DEV_TTY_H
#define DEV_TTY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <flanterm.h>
#include <fs/vfs.h>
#include <sys/spinlock.h>

#define TTY_MAJOR 4
#define TTY_MAX 8
#define TTY_IBUF_SIZE 512

/* c_iflag bits */
#define ICRNL 0x0001 /* map CR to NL on input   */
#define INLCR 0x0002 /* map NL to CR on input   */
#define IGNCR 0x0004 /* ignore CR               */

/* c_oflag bits */
#define OPOST 0x0001 /* output processing       */
#define ONLCR 0x0002 /* map NL to CR+NL         */
#define OCRNL 0x0004 /* map CR to NL            */

/* c_lflag bits */
#define ECHO 0x0001   /* echo input              */
#define ECHOE 0x0002  /* echo ERASE as BS-SP-BS  */
#define ICANON 0x0004 /* canonical (line) mode   */
#define ISIG 0x0008   /* generate signals        */

typedef struct tty_termios {
	uint32_t c_iflag; /* input flags  */
	uint32_t c_oflag; /* output flags */
	uint32_t c_lflag; /* local flags  */
} tty_termios_t;

#define TIOCGWINSZ 0x5413 /* get window size              */
#define TIOCSWINSZ 0x5414 /* set window size (ignored)    */
#define TCGETS 0x5401     /* get termios                  */
#define TCSETS 0x5402     /* set termios                  */

typedef struct tty_winsize {
	uint16_t ws_row;
	uint16_t ws_col;
	uint16_t ws_xpixel; /* unused */
	uint16_t ws_ypixel; /* unused */
} tty_winsize_t;

typedef struct tty {
	int index;
	struct flanterm_context *ft_ctx;

	/* input ring buffer */
	char ibuf[TTY_IBUF_SIZE];
	size_t ibuf_head; /* write index (producer)            */
	size_t ibuf_tail; /* read  index (consumer)            */

	tty_termios_t termios;
	spinlock_t lock;

	int open_count;
	bool initialised;
} tty_t;

void tty_init(void *fb_addr,
              size_t fb_width,
              size_t fb_height,
              size_t fb_pitch,
              uint8_t red_mask_size,
              uint8_t red_mask_shift,
              uint8_t green_mask_size,
              uint8_t green_mask_shift,
              uint8_t blue_mask_size,
              uint8_t blue_mask_shift);

tty_t *tty_get_active(void);
int tty_switch(int n);
void tty_input_byte(tty_t *tty, char c);

#endif // DEV_TTY_H