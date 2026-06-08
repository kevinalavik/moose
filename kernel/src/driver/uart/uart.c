#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <arch/cpu.h>
#include <dev/device.h>
#include <uart/uart.h>
#include <tty/tty.h>
#include <sys/klog.h>

#define UART_DATA 0
#define UART_IER 1
#define UART_DLL 0
#define UART_DLH 1
#define UART_FCR 2
#define UART_LCR 3
#define UART_MCR 4
#define UART_LSR 5
#define UART_LCR_DLAB 0x80
#define UART_LSR_DATA_READY 0x01
#define UART_LSR_THRE 0x20
#define UART_MCR_DTR 0x01
#define UART_MCR_RTS 0x02
#define UART_MCR_OUT1 0x04
#define UART_MCR_OUT2 0x08
#define UART_MCR_LOOP 0x10
#define UART_TIMEOUT 100000

#define X(name_, port_, label_) { .label = label_, .port = (uint16_t)(port_) },
static com_dev_t com_devices[] = { COM_DEVICES };
#undef X

#define COM_DEVICE_COUNT (sizeof(com_devices) / sizeof(com_devices[0]))

static com_dev_t *uart_get_dev(com_port_t port)
{
	for (size_t i = 0; i < COM_DEVICE_COUNT; i++) {
		if (com_devices[i].port == (uint16_t)port)
			return &com_devices[i];
	}
	klog("uart", "unknown uart port %#x", port);
	return NULL;
}

static bool uart_can_read(uint16_t port)
{
	return (inb(port + UART_LSR) & UART_LSR_DATA_READY) != 0;
}

static bool uart_can_write(uint16_t port)
{
	return (inb(port + UART_LSR) & UART_LSR_THRE) != 0;
}

static bool uart_wait_write(uint16_t port)
{
	for (size_t i = 0; i < UART_TIMEOUT; i++) {
		if (uart_can_write(port))
			return true;
	}
	return false;
}

static bool uart_wait_read(uint16_t port)
{
	for (size_t i = 0; i < UART_TIMEOUT; i++) {
		if (uart_can_read(port))
			return true;
	}
	return false;
}

static bool uart_write_byte(uint16_t port, uint8_t byte)
{
	if (!uart_wait_write(port))
		return false;
	outb(port + UART_DATA, byte);
	return true;
}

static bool uart_read_byte(uint16_t port, uint8_t *byte)
{
	if (byte == NULL)
		return false;
	if (!uart_wait_read(port))
		return false;
	*byte = inb(port + UART_DATA);
	return true;
}

static bool uart_probe(uint16_t port)
{
	outb(port + UART_MCR,
	     UART_MCR_LOOP | UART_MCR_OUT2 | UART_MCR_RTS | UART_MCR_DTR);
	outb(port + UART_DATA, 0xAE);
	if (inb(port + UART_DATA) != 0xAE)
		return false;
	outb(port + UART_MCR, UART_MCR_OUT2 | UART_MCR_RTS | UART_MCR_DTR);
	return true;
}

static void uart_hw_init(uint16_t port)
{
	outb(port + UART_IER, 0x00);
	outb(port + UART_LCR, UART_LCR_DLAB);
	outb(port + UART_DLL, 0x03);
	outb(port + UART_DLH, 0x00);
	outb(port + UART_LCR, 0x03);
	outb(port + UART_FCR, 0xC7);
	outb(port + UART_MCR, UART_MCR_OUT2 | UART_MCR_RTS | UART_MCR_DTR);
}

static size_t uart_tty_write(void *priv, const void *buf, size_t len)
{
	com_dev_t *dev = priv;
	const uint8_t *bytes = buf;
	size_t written = 0;

	for (size_t i = 0; i < len; i++) {
		if (bytes[i] == '\n') {
			if (!uart_write_byte(dev->port, '\r'))
				break;
		}
		if (!uart_write_byte(dev->port, bytes[i]))
			break;
		written++;
	}

	if (written != len)
		klog("uart", "write timeout on %s", dev->label);

	return written;
}

static size_t uart_tty_read(void *priv, void *buf, size_t len)
{
	com_dev_t *dev = priv;
	uint8_t *bytes = buf;
	size_t read = 0;

	for (size_t i = 0; i < len; i++) {
		if (!uart_read_byte(dev->port, &bytes[i]))
			break;
		read++;
	}
	return read;
}

static const tty_ops_t uart_tty_ops = {
	.write = uart_tty_write,
	.read = uart_tty_read,
	.control = NULL,
};

int uart_init(device_t *dev, com_port_t port)
{
	com_dev_t *cdev = uart_get_dev(port);
	if (!cdev)
		return -1;

	uart_hw_init(cdev->port);
	if (!uart_probe(cdev->port)) {
		klog("uart", "probe failed for %s at %#x", cdev->label,
		     cdev->port);
		return -1;
	}
	dev->chardev = tty_register(cdev->label, &uart_tty_ops, cdev);
	return char_dev_valid(&dev->chardev) ? 0 : -1;
}
