#ifndef DEV_UART_H
#define DEV_UART_H

#include <stddef.h>
#include <stdint.h>
#include <dev/dev.h>

/* super basic uart driver thanks to: https://wiki.osdev.org/UART */
#define COM_DEVICES        \
    X(COM1, 0x3F8, "COM1") \
    X(COM2, 0x2F8, "COM2") \
    X(COM3, 0x3E8, "COM3") \
    X(COM4, 0x2E8, "COM4") \
    X(COM5, 0x5F8, "COM5") \
    X(COM6, 0x4F8, "COM6") \
    X(COM7, 0x5E8, "COM7") \
    X(COM8, 0x4E8, "COM8")

#define X(name_, port_, label_) name_ = port_,
typedef enum
{
    COM_DEVICES
} com_port_t;
#undef X

typedef struct com_dev
{
    const char *label;
    uint16_t port;
} com_dev_t;

handle_t uart_init(com_port_t port);

#endif /* DEV_UART_H */