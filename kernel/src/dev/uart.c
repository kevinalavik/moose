#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <arch/cpu.h>
#include <dev/uart.h>
#include <dev/tty.h>
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
#define UART_TIMEOUT 100000 /* small timeout for writes and shit */

#define X(name_, port_, label_) {.label = label_, .port = (uint16_t)(port_)},
static com_dev_t com_devices[] = {
    COM_DEVICES};
#undef X

#define COM_DEVICE_COUNT (sizeof(com_devices) / sizeof(com_devices[0]))

static com_dev_t *uart_get_dev(com_port_t port)
{
    for (size_t i = 0; i < COM_DEVICE_COUNT; i++)
    {
        if (com_devices[i].port == (uint16_t)port)
            return &com_devices[i];
    }

    klog("uart", COL_AMBER "unknown uart port %#x" COL_RESET, port);
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
    for (size_t i = 0; i < UART_TIMEOUT; i++)
    {
        if (uart_can_write(port))
            return true;
    }

    return false;
}

static bool uart_wait_read(uint16_t port)
{
    for (size_t i = 0; i < UART_TIMEOUT; i++)
    {
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
    outb(port + UART_MCR, UART_MCR_LOOP | UART_MCR_OUT2 | UART_MCR_RTS | UART_MCR_DTR);
    outb(port + UART_DATA, 0xAE);

    if (inb(port + UART_DATA) != 0xAE)
        return false;

    outb(port + UART_MCR, UART_MCR_OUT2 | UART_MCR_RTS | UART_MCR_DTR);
    return true;
}

/* the comments come from the yapping in Example Code in https://wiki.osdev.org/Serial_Ports */
static void uart_hw_init(uint16_t port)
{
    /* disable ints */
    outb(port + UART_IER, 0x00);

    /* enable dlab (to set rate divisor, idk what this shit means. And se baud to 38400) */
    outb(port + UART_LCR, UART_LCR_DLAB);
    outb(port + UART_DLL, 0x03);
    outb(port + UART_DLH, 0x00);

    /* 8 bits + no parity + one stop bit. */
    outb(port + UART_LCR, 0x03);

    /* Enable FIFO + clear them + 14-byte threshold. */
    outb(port + UART_FCR, 0xC7);

    /* enable irq and rts and dtr */
    outb(port + UART_MCR, UART_MCR_OUT2 | UART_MCR_RTS | UART_MCR_DTR);
}

static size_t uart_read(handle_t *handle, void *buf, size_t len)
{
    if (handle == NULL || handle->dev == NULL || buf == NULL)
    {
        dev_error("uart read with invalid args");
        return 0;
    }

    com_dev_t *dev = handle->dev;
    uint8_t *bytes = buf;
    size_t read = 0;

    for (size_t i = 0; i < len; i++)
    {
        if (!uart_read_byte(dev->port, &bytes[i]))
            break;
        read++;
    }

    return read;
}

static size_t uart_write(handle_t *handle, const void *buf, size_t len)
{
    if (handle == NULL || handle->dev == NULL || buf == NULL)
    {
        dev_error("uart write with invalid args");
        return 0;
    }

    com_dev_t *dev = handle->dev;
    const uint8_t *bytes = buf;
    size_t written = 0;

    for (size_t i = 0; i < len; i++)
    {
        if (bytes[i] == '\n')
        {
            if (!uart_write_byte(dev->port, '\r'))
                break;
        }

        if (!uart_write_byte(dev->port, bytes[i]))
            break;
        written++;
    }

    if (written != len)
    {
        klog("uart",
             COL_AMBER "uart write timeout on %s" COL_RESET,
             dev->label);
    }

    return written;
}

static const dev_ops_t uart_ops = {
    .read = uart_read,
    .write = uart_write,
};

handle_t uart_init(com_port_t port)
{
    com_dev_t *dev = uart_get_dev(port);

    if (dev == NULL)
        return HANDLE_INVALID;

    uart_hw_init(dev->port);
    if (!uart_probe(dev->port))
    {
        klog("uart", COL_BRED "hw error: uart probe failed for %s at %#x" COL_RESET, dev->label, dev->port);
        return HANDLE_INVALID;
    }
    return device_handle_make(dev, &uart_ops, dev->label);
}