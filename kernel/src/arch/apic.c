#include <arch/apic.h>
#include <arch/acpi.h>
#include <arch/cpu.h>
#include <mm/vma.h>
#include <sys/klog.h>
#include <sys/moose.h>
#include <sys/errno.h>

#define LAPIC_ID 0x020
#define LAPIC_VERSION 0x030
#define LAPIC_TPR 0x080
#define LAPIC_EOI 0x0B0
#define LAPIC_LDR 0x0D0
#define LAPIC_DFR 0x0E0
#define LAPIC_SVR 0x0F0
#define LAPIC_ESR 0x280
#define LAPIC_LVT_CMCI 0x2F0
#define LAPIC_ICR_LO 0x300
#define LAPIC_ICR_HI 0x310
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_LVT_THERM 0x330
#define LAPIC_LVT_PERF 0x340
#define LAPIC_LVT_LINT0 0x350
#define LAPIC_LVT_LINT1 0x360
#define LAPIC_LVT_ERROR 0x370
#define LAPIC_TIMER_ICR 0x380
#define LAPIC_TIMER_CCR 0x390
#define LAPIC_TIMER_DCR 0x3E0

#define SVR_ENABLE (1 << 8)

#define LVT_MASKED (1 << 16)
#define LVT_FIXED 0
#define LVT_NMI (4 << 8)
#define LVT_EXTINT (7 << 8)

#define LVT_POLARITY_LOW (1 << 13)
#define LVT_TRIGGER_LEVEL (1 << 15)

#define IOAPIC_VER 0x01
#define IOAPIC_REDIR_BASE 0x10

#define SPURIOUS_VECTOR 0xFF
#define ERROR_VECTOR 0xFE

#define MAX_IOAPIC 16

static volatile uint32_t *lapic_base;

static volatile uint32_t *ioapic_bases[MAX_IOAPIC];
static uint32_t ioapic_gsi_bases[MAX_IOAPIC];
static uint8_t ioapic_max_redirs[MAX_IOAPIC];

static inline uint32_t lapic_read(uint16_t reg)
{
	return *(volatile uint32_t *)((uintptr_t)lapic_base + reg);
}

static inline void lapic_write(uint16_t reg, uint32_t val)
{
	*(volatile uint32_t *)((uintptr_t)lapic_base + reg) = val;
}

static inline void ioapic_write(volatile uint32_t *base, uint8_t reg,
				uint32_t val)
{
	base[0] = reg;
	*(volatile uint32_t *)((uintptr_t)base + 0x10) = val;
}

static inline uint32_t ioapic_read(volatile uint32_t *base, uint8_t reg)
{
	base[0] = reg;
	return *(volatile uint32_t *)((uintptr_t)base + 0x10);
}

static void pic_remap_and_mask(void)
{
	outb(0x20, 0x11);
	outb(0xA0, 0x11);
	outb(0x21, 0x20);
	outb(0xA1, 0x28);
	outb(0x21, 0x04);
	outb(0xA1, 0x02);
	outb(0x21, 0x01);
	outb(0xA1, 0x01);
	outb(0x21, 0xFF);
	outb(0xA1, 0xFF);
	klog("APIC", "8259 PIC remapped and masked");
}

static void lapic_enable(void)
{
	uint32_t eax, edx;
	__asm__ volatile("rdmsr" : "=a"(eax), "=d"(edx) : "c"(0x1B));
	eax |= (1 << 11);
	__asm__ volatile("wrmsr" : : "a"(eax), "d"(edx), "c"(0x1B));
}

static void lapic_configure_lint(void)
{
	lapic_write(LAPIC_LVT_LINT0, LVT_MASKED);
	lapic_write(LAPIC_LVT_LINT1, LVT_MASKED);
	lapic_write(LAPIC_LVT_ERROR, LVT_MASKED | ERROR_VECTOR);
	lapic_write(LAPIC_LVT_PERF, LVT_MASKED);
	lapic_write(LAPIC_LVT_THERM, LVT_MASKED);
	lapic_write(LAPIC_LVT_CMCI, LVT_MASKED);

	for (size_t i = 0; i < lapic_nmi_count; i++) {
		if (lapic_nmi[i].uid != 0 && lapic_nmi[i].uid != 0xFF)
			continue;

		uint32_t lvt = LVT_NMI;
		uint16_t fl = lapic_nmi[i].flags;

		switch (fl & ACPI_MADT_POLARITY_MASK) {
		case ACPI_MADT_POLARITY_ACTIVE_LOW:
			lvt |= LVT_POLARITY_LOW;
			break;
		case ACPI_MADT_POLARITY_ACTIVE_HIGH:
		case ACPI_MADT_POLARITY_CONFORMING:
		default:
			break;
		}

		switch (fl & ACPI_MADT_TRIGGERING_MASK) {
		case ACPI_MADT_TRIGGERING_LEVEL:
			lvt |= LVT_TRIGGER_LEVEL;
			break;
		case ACPI_MADT_TRIGGERING_EDGE:
		case ACPI_MADT_TRIGGERING_CONFORMING:
		default:
			break;
		}

		if (lapic_nmi[i].lint == 0)
			lapic_write(LAPIC_LVT_LINT0, lvt);
		else if (lapic_nmi[i].lint == 1)
			lapic_write(LAPIC_LVT_LINT1, lvt);
	}

	if (madt->flags & ACPI_PCAT_COMPAT) {
		if (lapic_read(LAPIC_LVT_LINT0) == LVT_MASKED)
			lapic_write(LAPIC_LVT_LINT0, LVT_EXTINT);
	}
}

static void ioapic_set_entry(volatile uint32_t *base, uint8_t idx,
			     uint8_t vector, uint16_t flags, int masked)
{
	uint32_t lo = vector;
	uint32_t hi = 0;

	switch (flags & ACPI_MADT_POLARITY_MASK) {
	case ACPI_MADT_POLARITY_ACTIVE_LOW:
		lo |= LVT_POLARITY_LOW;
		break;
	case ACPI_MADT_POLARITY_ACTIVE_HIGH:
	case ACPI_MADT_POLARITY_CONFORMING:
	default:
		break;
	}

	switch (flags & ACPI_MADT_TRIGGERING_MASK) {
	case ACPI_MADT_TRIGGERING_LEVEL:
		lo |= LVT_TRIGGER_LEVEL;
		break;
	case ACPI_MADT_TRIGGERING_EDGE:
	case ACPI_MADT_TRIGGERING_CONFORMING:
	default:
		break;
	}

	if (masked)
		lo |= LVT_MASKED;

	ioapic_write(base, IOAPIC_REDIR_BASE + idx * 2, lo);
	ioapic_write(base, IOAPIC_REDIR_BASE + idx * 2 + 1, hi);
}

static int ioapic_init_one(size_t idx)
{
	uint64_t addr = ioapic[idx].address;
	uint32_t gsi_base = ioapic[idx].gsi_base;

	volatile uint32_t *base = (volatile uint32_t *)vma_ioremap(
		current_vctx, addr, 0x1000, VMA_PROT_READ | VMA_PROT_WRITE);
	if (!base) {
		klog("APIC",
		     COL_RED "IOAPIC[%zu]: ioremap 0x%x failed" COL_RESET, idx,
		     (uint32_t)addr);
		return -ENOMEM;
	}

	uint32_t ver = ioapic_read(base, IOAPIC_VER);
	uint8_t max_redirs = (ver >> 16) & 0xFF;

	ioapic_bases[idx] = base;
	ioapic_gsi_bases[idx] = gsi_base;
	ioapic_max_redirs[idx] = max_redirs;

	klog("IOAPIC", "[%zu] id=%u addr=0x%x gsi_base=%u max_entry=%u", idx,
	     ioapic[idx].id, (uint32_t)addr, gsi_base, max_redirs);

	for (uint8_t i = 0; i <= max_redirs; i++) {
		uint32_t gsi = gsi_base + i;

		uint16_t flags = 0;
		for (size_t j = 0; j < iso_count; j++) {
			if (iso[j].gsi == gsi) {
				flags = iso[j].flags;
				break;
			}
		}

		uint8_t vector = SPURIOUS_VECTOR;
		if (gsi < 0xE0)
			vector = IRQ_BASE + (uint8_t)gsi;

		ioapic_set_entry(base, i, vector, flags, 1);
	}

	return 0;
}

int apic_init(void)
{
	uint64_t lapic_addr;
	if (has_lapic_override && lapic_override.address)
		lapic_addr = lapic_override.address;
	else
		lapic_addr = madt ? madt->local_interrupt_controller_address :
				    0;

	if (!lapic_addr)
		lapic_addr = 0xFEE00000;

	lapic_base = (volatile uint32_t *)vma_ioremap(
		current_vctx, lapic_addr, 0x1000,
		VMA_PROT_READ | VMA_PROT_WRITE);
	if (!lapic_base) {
		klog("APIC", COL_RED "failed to map LAPIC @ 0x%llx" COL_RESET,
		     (unsigned long long)lapic_addr);
		return -ENOMEM;
	}

	lapic_enable();

	uint32_t version = lapic_read(LAPIC_VERSION);

	pic_remap_and_mask();

	lapic_configure_lint();

	lapic_write(LAPIC_TPR, 0);
	lapic_write(LAPIC_SVR, SPURIOUS_VECTOR | SVR_ENABLE);
	lapic_read(LAPIC_SVR);

	lapic_write(LAPIC_ESR, 0);
	lapic_read(LAPIC_ESR);
	lapic_write(LAPIC_ESR, 0);
	lapic_read(LAPIC_ESR);

	for (size_t i = 0; i < ioapic_count; i++) {
		if (ioapic_init_one(i) != 0)
			klog("APIC",
			     COL_AMBER "IOAPIC[%zu] init failed" COL_RESET, i);
	}

	klog("APIC", "LAPIC version=0x%x addr=0x%llx init=ok", version,
	     (unsigned long long)lapic_addr);
	return 0;
}

void apic_eoi(void)
{
	lapic_write(LAPIC_EOI, 0);
}

int apic_gsi_set_mask(uint32_t gsi, int masked)
{
	for (size_t i = 0; i < MAX_IOAPIC; i++) {
		if (!ioapic_bases[i])
			continue;

		uint32_t gsi_end = ioapic_gsi_bases[i] + ioapic_max_redirs[i];
		if (gsi < ioapic_gsi_bases[i] || gsi > gsi_end)
			continue;

		uint8_t redir = (uint8_t)(gsi - ioapic_gsi_bases[i]);
		uint8_t reg = IOAPIC_REDIR_BASE + redir * 2;
		uint32_t lo = ioapic_read(ioapic_bases[i], reg);
		if (masked)
			lo |= LVT_MASKED;
		else
			lo &= ~LVT_MASKED;
		ioapic_write(ioapic_bases[i], reg, lo);
		return 0;
	}
	return -ENOENT;
}
