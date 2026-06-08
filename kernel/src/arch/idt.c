#include <arch/idt.h>
#include <arch/apic.h>
#include <arch/cpu.h>
#include <sys/klog.h>
#include <tty/tty.h>
#include <arch/gdt.h>
#include <stdbool.h>
#include <mm/vma.h>

__attribute__((aligned(16))) static idt_entry_t idt[256];
static idtr_t idtr;

static bool warned[256];

static irq_handler_t irq_handlers[256];

extern void *isr_stub_table[];
static int exception_handler(int_frame_t *frame)
{
	if (frame->vector == 14) {
		uint64_t fault_addr = read_cr2();
		bool is_write = frame->error_code & 0x2;
		bool is_user = frame->error_code & 0x4;

		if (current_vctx && vma_handle_fault(current_vctx, fault_addr,
						     is_write, is_user) == 0)
			return 0;
	}

	klog("panic",
	     COL_BRED
	     "exception vector=%lu error=%02x rip=0x%.16llx rsp=0x%.16llx cr2=0x%.16llx" COL_RESET,
	     frame->vector, frame->error_code, frame->rip, frame->rsp,
	     (frame->vector == 14) ? read_cr2() : 0);
	hcf();
	__builtin_unreachable();
}

int irq_register(uint8_t vector, irq_handler_t handler)
{
	if (vector > 0xFE)
		return -1;
	irq_handlers[vector] = handler;
	return 0;
}

void interrupt_handler(int_frame_t *frame)
{
	if (frame->vector < 32) {
		if (exception_handler(frame) == 0)
			return;
		hcf();
	}

	irq_handler_t handler = irq_handlers[frame->vector];
	if (handler) {
		handler(frame);
		apic_eoi();
		return;
	}

	if (!warned[frame->vector]) {
		warned[frame->vector] = true;
		klog("idt",
		     COL_AMBER
		     "unhandled interrupt vector=%lu rip=%#lx" COL_RESET,
		     frame->vector, frame->rip);
	}
	apic_eoi();
}

void idt_set_descriptor(uint8_t vector, void *isr, uint8_t flags)
{
	idt_entry_t *descriptor = &idt[vector];

	descriptor->isr_low = (uint64_t)isr & 0xFFFF;
	descriptor->kernel_cs = GDT_KCODE_SEL;
	descriptor->ist = 0;
	descriptor->attributes = flags;
	descriptor->isr_mid = ((uint64_t)isr >> 16) & 0xFFFF;
	descriptor->isr_high = ((uint64_t)isr >> 32) & 0xFFFFFFFF;
	descriptor->reserved = 0;
}

void idt_init()
{
	idtr.base = (uintptr_t)&idt[0];
	idtr.limit = (uint16_t)sizeof(idt_entry_t) * 256 - 1;

	for (uint16_t vector = 0; vector < 256; vector++)
		idt_set_descriptor((uint8_t)vector, isr_stub_table[vector],
				   0x8E);

	__asm__ volatile("lidt %0" : : "m"(idtr));
	sti();

	klog("idt", "loaded idt @ %p", &idtr);
}