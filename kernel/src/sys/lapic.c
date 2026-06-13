#include <sys/apic.h>
#include <arch/cpu.h>
#include <arch/cpuid.h>
#include <mm/vma.h>
#include <mm/pfn.h>
#include <lib/printk.h>
#include <sys/panic.h>

#define IA32_APIC_BASE_MSR 0x1B
#define APIC_BASE_ENABLE (1ULL << 11)
#define APIC_BASE_X2APIC (1ULL << 10)
#define APIC_BASE_ADDR_MASK 0x000FFFFFFFFFF000ULL

static volatile uint32_t *lapic_mmio;
static int lapic_x2apic;

/* configure LINTn pins as NMI per the MADT "Local APIC NMI" entries that
 * apply to this CPU (uid == 0xFF means "all processors") any LINT pin not
 * covered by such an entry is left masked. */
static void lapic_configure_nmi_lints(uint32_t apic_id)
{
	uint8_t uid = 0xFF;

	for (size_t i = 0; i < lapic_count; i++) {
		if (lapic[i].id == apic_id) {
			uid = lapic[i].uid;
			break;
		}
	}

	for (size_t i = 0; i < lapic_nmi_count; i++) {
		struct acpi_madt_lapic_nmi *e = &lapic_nmi[i];

		if (e->uid != 0xFF && e->uid != uid)
			continue;

		uint32_t lvt = LAPIC_LVT_NMI;

		if ((e->flags & ACPI_MADT_POLARITY_MASK) == ACPI_MADT_POLARITY_ACTIVE_LOW)
			lvt |= (1u << 13); /* INTPOL: active low */

		if ((e->flags & ACPI_MADT_TRIGGERING_MASK) == ACPI_MADT_TRIGGERING_LEVEL)
			lvt |= (1u << 15); /* trigger mode: level */

		uint32_t reg = (e->lint == 0) ? LAPIC_REG_LVT_LINT0 : LAPIC_REG_LVT_LINT1;
		lapic_write(reg, lvt);

		printk("apic: lapic: LINT%u configured as NMI (uid=%u flags=0x%x)\n",
		       e->lint,
		       e->uid,
		       e->flags);
	}
}

void lapic_init(void)
{
	uint32_t eax, ebx, ecx, edx;

	cpuid(CPUID_FEATURES, 0, &eax, &ebx, &ecx, &edx);
	int x2apic_supported = (ecx >> 21) & 1;

	uint64_t apic_base_msr = rdmsr(IA32_APIC_BASE_MSR);
	uint64_t phys = apic_base_msr & APIC_BASE_ADDR_MASK;

	/* if the MADT specifies a 64-bit override address that differs from
	 * what's currently programmed, point the MSR at it before we map it. */
	if (has_lapic_override && lapic_override.address != phys) {
		phys = lapic_override.address;
		apic_base_msr =
		    (apic_base_msr & ~APIC_BASE_ADDR_MASK) | (phys & APIC_BASE_ADDR_MASK);
		wrmsr(IA32_APIC_BASE_MSR, apic_base_msr);
	}

	if (x2apic_supported) {
		wrmsr(IA32_APIC_BASE_MSR, apic_base_msr | APIC_BASE_ENABLE | APIC_BASE_X2APIC);
		lapic_x2apic = 1;
		printk("apic: lapic mode = x2APIC\n");
	} else {
		if (!lapic_mmio)
			lapic_mmio = (volatile uint32_t *)vmap_mmio(kernel_vctx, phys, PAGE_SIZE);

		wrmsr(IA32_APIC_BASE_MSR, apic_base_msr | APIC_BASE_ENABLE);
		printk("apic: lapic mode = xAPIC, mmio_phys=0x%llx mmio_virt=%p\n",
		       (unsigned long long)phys,
		       (void *)lapic_mmio);
	}

	/* enable the LAPIC and program the spurious interrupt vector */
	lapic_write(LAPIC_REG_SVR, LAPIC_SPURIOUS_VECTOR | LAPIC_SVR_ENABLE);

	/* mask every local interrupt source by default, the NMI configuration
	 * below may unmask LINT0/LINT1 if the MADT says they're wired to NMI */
	lapic_write(LAPIC_REG_LVT_LINT0, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_REG_LVT_LINT1, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_REG_LVT_ERROR, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_REG_LVT_PERF, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_REG_LVT_THERMAL, LAPIC_LVT_MASKED);

	/* accept all interrupt priorities */
	lapic_write(LAPIC_REG_TPR, 0);

	lapic_configure_nmi_lints(lapic_get_id());

	printk("apic: lapic id=%u version=0x%x\n", lapic_get_id(), lapic_read(LAPIC_REG_VERSION));
}

uint32_t lapic_read(uint32_t reg)
{
	if (lapic_x2apic)
		return (uint32_t)rdmsr(0x800 + (reg >> 4));

	if (!lapic_mmio)
		panic(NULL, "apic: lapic_read(0x%x) before lapic_init", reg);

	return lapic_mmio[reg / 4];
}

void lapic_write(uint32_t reg, uint32_t value)
{
	if (lapic_x2apic) {
		wrmsr(0x800 + (reg >> 4), value);
		return;
	}

	if (!lapic_mmio)
		panic(NULL, "apic: lapic_write(0x%x) before lapic_init", reg);

	lapic_mmio[reg / 4] = value;
}

void lapic_eoi(void)
{
	lapic_write(LAPIC_REG_EOI, 0);
}

uint32_t lapic_get_id(void)
{
	if (lapic_x2apic)
		return lapic_read(LAPIC_REG_ID); /* x2APIC: full 32-bit id, no shift */

	return lapic_read(LAPIC_REG_ID) >> 24; /* xAPIC: id is in bits 24-31 */
}

void lapic_send_ipi(uint32_t apic_id, uint8_t vector)
{
	if (lapic_x2apic) {
		uint64_t val = ((uint64_t)apic_id << 32) | vector;
		wrmsr(0x800 + (LAPIC_REG_ICR_LOW >> 4), val);
		return;
	}

	lapic_write(LAPIC_REG_ICR_HIGH, apic_id << 24);
	lapic_write(LAPIC_REG_ICR_LOW, vector);
}