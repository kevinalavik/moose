#include <ps2/keyboard.h>
#include <dev/device.h>
#include <dev/platform.h>
#include <arch/apic.h>
#include <arch/cpu.h>
#include <arch/idt.h>
#include <sys/klog.h>
#include <util/printf.h>

#define PS2_DATA 0x60
#define PS2_STATUS 0x64

#define PS2_STAT_OUT_FULL 1
#define PS2_STAT_IBF (1 << 1)

#define PS2_ACK 0xFA
#define PS2_BAT_OK 0xAA

#define PS2_CMD_RESET 0xFF
#define PS2_CMD_ENABLE_SCAN 0xF4
#define PS2_CMD_SET_DEFAULTS 0xF6

#define PS2_TIMEOUT 100000

static int ps2_wait_write(void)
{
	for (int i = 0; i < PS2_TIMEOUT; i++) {
		if (!(inb(PS2_STATUS) & PS2_STAT_IBF))
			return 0;
	}
	return -1;
}

static int ps2_wait_read(void)
{
	for (int i = 0; i < PS2_TIMEOUT; i++) {
		if (inb(PS2_STATUS) & PS2_STAT_OUT_FULL)
			return 0;
	}
	return -1;
}

static int ps2_write_data(uint8_t data)
{
	if (ps2_wait_write() < 0)
		return -1;
	outb(PS2_DATA, data);
	return 0;
}

static int ps2_read_data(uint8_t *out)
{
	if (ps2_wait_read() < 0)
		return -1;
	*out = inb(PS2_DATA);
	return 0;
}

static void ps2_flush(void)
{
	for (int i = 0; i < PS2_TIMEOUT; i++) {
		if (!(inb(PS2_STATUS) & PS2_STAT_OUT_FULL))
			break;
		inb(PS2_DATA);
	}
}

static int ps2_expect_ack(void)
{
	uint8_t b;
	if (ps2_read_data(&b) < 0)
		return -1;
	return b == PS2_ACK ? 0 : -1;
}

static int ps2_wait_bat(void)
{
	uint8_t b;
	for (int i = 0; i < PS2_TIMEOUT; i++) {
		if (ps2_read_data(&b) == 0) {
			if (b == PS2_BAT_OK)
				return 0;
			if (b != PS2_ACK)
				return -1;
		}
	}
	return -1;
}

static const char sc_norm[128] = {
	0,   0,	  '1',	'2',  '3',  '4', '5', '6',  '7', '8', '9', '0',
	'-', '=', '\b', '\t', 'q',  'w', 'e', 'r',  't', 'y', 'u', 'i',
	'o', 'p', '[',	']',  '\n', 0,	 'a', 's',  'd', 'f', 'g', 'h',
	'j', 'k', 'l',	';',  '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
	'b', 'n', 'm',	',',  '.',  '/', 0,   '*',  0,	 ' ', 0,
};

static const char sc_shift[128] = {
	0,   0,	  '!',	'@',  '#',  '$', '%', '^', '&', '*', '(', ')',
	'_', '+', '\b', '\t', 'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',
	'O', 'P', '{',	'}',  '\n', 0,	 'A', 'S', 'D', 'F', 'G', 'H',
	'J', 'K', 'L',	':',  '"',  '~', 0,   '|', 'Z', 'X', 'C', 'V',
	'B', 'N', 'M',	'<',  '>',  '?', 0,   '*', 0,	' ', 0,
};

static int lshift;
static int rshift;
static int caps;
static int extended;
static int kbd_present;

static int ps2_keyboard_init(void)
{
	ps2_flush();

	if (ps2_write_data(PS2_CMD_RESET) < 0)
		return -1;
	if (ps2_expect_ack() < 0)
		return -1;
	if (ps2_wait_bat() < 0)
		return -1;

	if (ps2_write_data(PS2_CMD_SET_DEFAULTS) < 0)
		return -1;
	if (ps2_expect_ack() < 0)
		return -1;

	if (ps2_write_data(PS2_CMD_ENABLE_SCAN) < 0)
		return -1;
	if (ps2_expect_ack() < 0)
		return -1;

	ps2_flush();
	return 0;
}

static void ps2kbd_irq(int_frame_t *frame)
{
	(void)frame;

	uint8_t status = inb(PS2_STATUS);
	if (!(status & 1))
		return;

	uint8_t scancode = inb(PS2_DATA);

	if (scancode == 0xE0) {
		extended = 1;
		return;
	}

	if (extended) {
		extended = 0;
		return;
	}

	if (scancode < 0x80) {
		switch (scancode) {
		case 0x2A:
			lshift = 1;
			return;
		case 0x36:
			rshift = 1;
			return;
		case 0x3A:
			caps = !caps;
			return;
		default:
			break;
		}

		int shift = lshift || rshift;
		char c = sc_norm[scancode];
		if (c >= 'a' && c <= 'z')
			shift ^= caps;
		c = shift ? sc_shift[scancode] : c;
		if (c)
			kprintf("%c", c);
	} else {
		uint8_t make = scancode - 0x80;
		switch (make) {
		case 0x2A:
			lshift = 0;
			return;
		case 0x36:
			rshift = 0;
			return;
		default:
			break;
		}
	}
}

static int ps2kbd_probe(device_t *dev)
{
	uint32_t gsi = 1;

	for (size_t i = 0; i < dev->num_resources; i++) {
		resource_t *r = &dev->resources[i];
		if (r->flags & IORESOURCE_IRQ) {
			gsi = (uint32_t)r->start;
			break;
		}
	}

	if (ps2_keyboard_init() < 0) {
		klog("ps2kbd", "no keyboard");
		return 0;
	}

	kbd_present = 1;
	uint8_t vector = IRQ_BASE + (uint8_t)gsi;
	irq_register(vector, ps2kbd_irq);
	apic_gsi_set_mask(gsi, 0);
	return 0;
}

static const platform_device_id_t ps2kbd_ids[] = {
	{ "PNP0300" }, { "PNP0301" }, { "PNP0302" }, { "PNP0303" },
	{ "PNP0304" }, { "PNP0305" }, { "PNP0306" }, { "PNP0307" },
	{ "PNP0308" }, { "PNP0309" }, { "PNP030A" }, { "PNP030B" },
	{ NULL },
};

static platform_driver_t ps2kbd_driver = {
	.driver = {
		.name   = "ps2kbd",
		.probe  = ps2kbd_probe,
		.remove = NULL,
	},
	.id_table = ps2kbd_ids,
};

int ps2kbd_init(void)
{
	return platform_driver_register(&ps2kbd_driver);
}
