#include <arch/idt.h>
#include <arch/cpu.h>
#include <sys/klog.h>
#include <lib/term.h>
#include <arch/gdt.h>

__attribute__((aligned(16))) static idt_entry_t idt[256];
static idtr_t idtr;

static bool vectors[256];
static bool warned[256];

extern void *isr_stub_table[];

__attribute__((noreturn)) void exception_handler(int_frame_t *frame)
{
    klog("panic", COL_BRED "exception vector=%lu error=%#lx rip=%#lx" COL_RESET, frame->vector, frame->error_code, frame->rip);
    hcf();
}

void interrupt_handler(int_frame_t *frame)
{
    if (frame->vector < 32)
        exception_handler(frame);

    /* dont spam on unhandled IRQs like handler */
    if (!warned[frame->vector])
    {
        warned[frame->vector] = true;
        klog("idt", COL_AMBER "unhandled interrupt vector=%lu rip=%#lx" COL_RESET, frame->vector, frame->rip);
    }
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
    {
        idt_set_descriptor((uint8_t)vector, isr_stub_table[vector], 0x8E);
        vectors[vector] = true;
    }

    __asm__ volatile("lidt %0" : : "m"(idtr));
    __asm__ volatile("sti");

    klog("idt", COL_STEEL "loaded idt @ %p" COL_RESET, &idtr);
}