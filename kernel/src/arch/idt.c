#include <arch/idt.h>
#include <arch/cpu.h>
#include <arch/gdt.h>
#include <stdbool.h>
#include <lib/printk.h>
#include <sys/panic.h>
#include <sys/irq.h>
#include <sys/apic.h>

__attribute__((aligned(16))) static idt_entry_t idt[256];
static idtr_t idtr;
extern void *isr_stub_table[];

static void exception_handler(int_frame_t *frame)
{
	cli();
	panic(frame, "exception");
}

void interrupt_handler(int_frame_t *frame)
{
	cli();
	if (frame->vector < 32) {
		exception_handler(frame);
		return;
	}
	if (frame->vector >= IRQ_VECTOR_BASE && frame->vector < IRQ_VECTOR_BASE + IRQ_COUNT) {
		irq_dispatch(frame);
		return;
	}
	printk("sys: unhandled interrupt vector=%u rip=%p\n", frame->vector, (void *)frame->rip);
}

void idt_set_descriptor(uint8_t vector, void *isr, uint8_t flags)
{
	idt_entry_t *d = &idt[vector];
	uint64_t addr = (uint64_t)isr;
	d->isr_low = addr & 0xFFFF;
	d->kernel_cs = GDT_KCODE_SEL;
	d->ist = 0;
	d->attributes = flags;
	d->isr_mid = (addr >> 16) & 0xFFFF;
	d->isr_high = (addr >> 32) & 0xFFFFFFFF;
	d->reserved = 0;
}

void idt_init()
{
	idtr.base = (uintptr_t)&idt[0];
	idtr.limit = sizeof(idt_entry_t) * 256 - 1;

	for (uint16_t v = 0; v < 256; v++)
		idt_set_descriptor(v, isr_stub_table[v], 0x8E);

	__asm__ volatile("lidt %0" : : "m"(idtr));
	printk("sys: loaded idt base=%p limit=0x%x\n", (void *)idtr.base, idtr.limit);
}