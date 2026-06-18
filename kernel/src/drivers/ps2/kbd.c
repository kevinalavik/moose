#include <drivers/driver.h>
#include <dev/device.h>
#include <dev/acpi_bus.h>
#include <arch/io.h>
#include <sys/irq.h>
#include <dev/tty.h>
#include <lib/printk.h>

#define I8042_DATA 0x60
#define I8042_STATUS 0x64
#define I8042_CMD 0x64

#define I8042_STR_OBF (1 << 0)
#define I8042_STR_IBF (1 << 1)

#define I8042_CMD_READ_CTRL 0x20
#define I8042_CMD_WRITE_CTRL 0x60
#define I8042_CMD_SELF_TEST 0xAA
#define I8042_CMD_KBD_TEST 0xAB
#define I8042_CMD_KBD_ENABLE 0xAE
#define I8042_CMD_KBD_DISABLE 0xAD

#define KBD_CMD_SET_LEDS 0xED
#define KBD_CMD_SET_RATE 0xF3
#define KBD_CMD_SCAN_SET 0xF0
#define KBD_CMD_ENABLE 0xF4
#define KBD_CMD_RESET 0xFF

#define KBD_ACK 0xFA
#define KBD_BAT_SUCCESS 0xAA

static bool shift_pressed;
static bool ctrl_pressed;
static bool alt_pressed;
static bool extended;

static void i8042_wait_write(void)
{
	while (inb(I8042_STATUS) & I8042_STR_IBF)
		;
}

static void i8042_wait_read(void)
{
	while (!(inb(I8042_STATUS) & I8042_STR_OBF))
		;
}

static uint8_t i8042_read(void)
{
	i8042_wait_read();
	return inb(I8042_DATA);
}

static void i8042_write_cmd(uint8_t cmd)
{
	i8042_wait_write();
	outb(I8042_CMD, cmd);
}

static void i8042_write_data(uint8_t data)
{
	i8042_wait_write();
	outb(I8042_DATA, data);
}

/* Set 1 scancode -> ASCII */
static const char kbd_map[128] = {
    0,   0x1B, '1', '2', '3', '4', '5', '6', '7',  '8', '9', '0',  '-',  '=', 0x08, 0x09,
    'q', 'w',  'e', 'r', 't', 'y', 'u', 'i', 'o',  'p', '[', ']',  0x0A, 0,   'a',  's',
    'd', 'f',  'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,   '\\', 'z',  'x', 'c',  'v',
    'b', 'n',  'm', ',', '.', '/', 0,   '*', 0,    ' ', 0,   0,    0,    0,   0,    0,
    0,   0,    0,   0,   0,   0,   0,   0,   0,    0,   '-', 0,    0,    0,   '+',  0,
    0,   0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,
    0,   0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,
    0,   0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,
};

static const char kbd_map_shift[128] = {
    0,   0x1B, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',  '+', 0x08, 0x09,
    'Q', 'W',  'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0x0A, 0,   'A',  'S',
    'D', 'F',  'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|', 'Z',  'X', 'C',  'V',
    'B', 'N',  'M', '<', '>', '?', 0,   '*', 0,   ' ', 0,   0,   0,    0,   0,    0,
};

static void ps2_kbd_irq(int_frame_t *frame)
{
	(void)frame;

	uint8_t status = inb(I8042_STATUS);
	if (!(status & I8042_STR_OBF))
		return;

	uint8_t raw = inb(I8042_DATA);

	if (raw == 0xE0) {
		extended = true;
		return;
	}

	bool pressed = !(raw & 0x80);
	uint8_t scancode = raw & 0x7F;

	if (scancode == 0x2A || scancode == 0x36) {
		shift_pressed = pressed;
		return;
	}
	if (scancode == 0x1D) {
		ctrl_pressed = pressed;
		return;
	}
	if (scancode == 0x38) {
		alt_pressed = pressed;
		return;
	}

	if (!pressed)
		goto done;

	/* Ctrl + F1-F8: switch TTY */
	if (ctrl_pressed && scancode >= 0x3B && scancode <= 0x42) {
		tty_switch(scancode - 0x3B);
		goto done;
	}

	if (extended) {
		tty_t *tty = tty_get_active();
		if (tty) {
			switch (scancode) {
			case 0x48: /* Up   */
				tty_input_byte(tty, 0x1B);
				tty_input_byte(tty, '[');
				tty_input_byte(tty, 'A');
				break;
			case 0x50: /* Down */
				tty_input_byte(tty, 0x1B);
				tty_input_byte(tty, '[');
				tty_input_byte(tty, 'B');
				break;
			case 0x4D: /* Right */
				tty_input_byte(tty, 0x1B);
				tty_input_byte(tty, '[');
				tty_input_byte(tty, 'C');
				break;
			case 0x4B: /* Left */
				tty_input_byte(tty, 0x1B);
				tty_input_byte(tty, '[');
				tty_input_byte(tty, 'D');
				break;
			}
		}
		goto done;
	}

	if (scancode >= 128)
		goto done;

	char ch;
	if (shift_pressed)
		ch = kbd_map_shift[scancode];
	else
		ch = kbd_map[scancode];

	if (!ch)
		goto done;

	if (ctrl_pressed && ch >= 'a' && ch <= 'z')
		ch -= 'a' - 1;

	tty_t *tty = tty_get_active();
	if (tty)
		tty_input_byte(tty, ch);

done:
	extended = false;
}

static int ps2_kbd_probe(device_t *dev, const device_id_t *id)
{
	(void)dev;
	(void)id;

	log("ps2-kbd: probing on acpi\n");

	while (inb(I8042_STATUS) & I8042_STR_OBF)
		inb(I8042_DATA);

	i8042_write_cmd(I8042_CMD_READ_CTRL);
	uint8_t ctrl = i8042_read();
	ctrl |= (1 << 0);
	ctrl &= ~((1 << 4) | (1 << 6));
	i8042_write_cmd(I8042_CMD_WRITE_CTRL);
	i8042_write_data(ctrl);

	i8042_write_cmd(I8042_CMD_KBD_ENABLE);

	i8042_write_data(KBD_CMD_SCAN_SET);
	i8042_read();
	i8042_write_data(0x01);
	i8042_read();

	i8042_write_data(KBD_CMD_SET_RATE);
	i8042_read();
	i8042_write_data(0x00);
	i8042_read();

	i8042_write_data(KBD_CMD_ENABLE);
	i8042_read();

	irq_register(1, ps2_kbd_irq);

	log("ps2-kbd: ready (set 1, translate off)\n");
	return 0;
}

static const device_id_t ps2_kbd_ids[] = {
    {"PNP0303", 0},
    {NULL, 0},
};

static driver_t ps2_kbd_driver = {
    .name = "ps2-kbd",
    .id_table = ps2_kbd_ids,
    .probe = ps2_kbd_probe,
    .remove = NULL,
};

void ps2_kbd_init(void)
{
	driver_register(&acpi_bus, &ps2_kbd_driver);
}

DRIVER_INIT("ps2-kbd", ps2_kbd_init);
