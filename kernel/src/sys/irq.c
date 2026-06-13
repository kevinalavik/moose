#include <sys/irq.h>
#include <sys/apic.h>
#include <lib/printk.h>

static irq_handler_t irq_handlers[IRQ_COUNT];

void irq_register(uint8_t irq, irq_handler_t handler)
{
	if (irq >= IRQ_COUNT)
		return;

	irq_handlers[irq] = handler;
	uint32_t gsi = irq_resolve_gsi(irq);
	ioapic_unmask_gsi(gsi);
	printk("irq: registered handler for irq%u -> gsi%u\n", irq, gsi);
}

void irq_dispatch(int_frame_t *frame)
{
	if (frame->vector >= IRQ_VECTOR_BASE && frame->vector < IRQ_VECTOR_BASE + IRQ_COUNT) {
		uint8_t irq = frame->vector - IRQ_VECTOR_BASE;
		if (irq_handlers[irq])
			irq_handlers[irq](frame);
		lapic_eoi();
	}
}
