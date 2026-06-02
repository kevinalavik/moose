#include <arch/gdt.h>
#include <sys/klog.h>
#include <lib/term.h>

#define GDT_ENTRY(seg_base, seg_limit, access_byte, flags_byte) \
    {                                                           \
        .limit_low = (uint16_t)((seg_limit) & 0xFFFFu),         \
        .base_low = (uint16_t)((seg_base) & 0xFFFFu),           \
        .base_mid = (uint8_t)(((seg_base) >> 16) & 0xFFu),      \
        .access = (uint8_t)(access_byte),                       \
        .limit = (uint8_t)(((seg_limit) >> 16) & 0x0Fu),        \
        .flags = (uint8_t)(((flags_byte) >> 4) & 0x0Fu),        \
        .base_high = (uint8_t)(((seg_base) >> 24) & 0xFFu),     \
    }

static gdt_t kernel_gdt = {
    .entries = {
        GDT_ENTRY(0, 0, 0, 0), /* null */
        GDT_ENTRY(0, 0xffff,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE_OR_DATA | GDT_ACCESS_EXECUTABLE | GDT_ACCESS_RW,
                  GDT_FLAGS_GRAN_4K | GDT_FLAGS_LONG_MODE), /* kcode */
        GDT_ENTRY(0, 0xffff,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE_OR_DATA | GDT_ACCESS_RW,
                  GDT_FLAGS_GRAN_4K), /* kdata */

        /* todo: fill these, TSS is not a regular entry */
        GDT_ENTRY(0, 0, 0, 0), /* udata */
        GDT_ENTRY(0, 0, 0, 0), /* ucode */
        GDT_ENTRY(0, 0, 0, 0), /* tss_low */
        GDT_ENTRY(0, 0, 0, 0), /* tss_high */
    },
};

static gdt_r kernel_gdtr = {
    .base = (uintptr_t)&kernel_gdt,
    .size = sizeof(kernel_gdt.entries) - 1,
};

void gdt_init()
{
    __asm__ volatile(
        "lgdt %[gdtr]\n"
        "pushq %[csel]\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw %[dsel], %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        :
        : [gdtr] "m"(kernel_gdtr), [csel] "i"(GDT_KCODE_SEL),
          [dsel] "i"(GDT_KDATA_SEL)
        : "rax", "memory");
    klog("arch/gdt", ANSI_YELLOW "loaded kernel gdt @ %p" ANSI_RESET, &kernel_gdtr);
}