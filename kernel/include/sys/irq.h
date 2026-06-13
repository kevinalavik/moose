#ifndef SYS_IRQ_H
#define SYS_IRQ_H

#include <stdint.h>
#include <arch/idt.h>

#define IRQ_COUNT 16

typedef void (*irq_handler_t)(int_frame_t *);

void irq_register(uint8_t irq, irq_handler_t handler);
void irq_dispatch(int_frame_t *frame);

#endif // SYS_IRQ_H