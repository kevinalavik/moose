#include <sys/apic.h>
#include <mm/vma.h>
#include <mm/pfn.h>
#include <lib/printk.h>
#include <sys/panic.h>

/* one MMIO mapping per I/O APIC; each exposes only IOREGSEL (offset 0x00)
 * and IOWIN (offset 0x10), used to indirectly access its register file. */
static volatile uint32_t *ioapic_mmio[MAX_IOAPIC];

uint32_t ioapic_read(size_t idx, uint8_t reg)
{
	if (idx >= ioapic_count || !ioapic_mmio[idx])
		panic(NULL, "apic: ioapic_read: invalid ioapic index %u", (unsigned)idx);

	ioapic_mmio[idx][0] = reg;  /* IOREGSEL */
	return ioapic_mmio[idx][4]; /* IOWIN */
}

void ioapic_write(size_t idx, uint8_t reg, uint32_t value)
{
	if (idx >= ioapic_count || !ioapic_mmio[idx])
		panic(NULL, "apic: ioapic_write: invalid ioapic index %u", (unsigned)idx);

	ioapic_mmio[idx][0] = reg;
	ioapic_mmio[idx][4] = value;
}

static uint64_t ioapic_read_redir(size_t idx, uint8_t pin)
{
	uint32_t lo = ioapic_read(idx, IOAPIC_REG_REDTBL + pin * 2);
	uint32_t hi = ioapic_read(idx, IOAPIC_REG_REDTBL + pin * 2 + 1);
	return ((uint64_t)hi << 32) | lo;
}

static void ioapic_write_redir(size_t idx, uint8_t pin, uint64_t value)
{
	/* write the high half first so the entry never transiently becomes
	 * unmasked with a stale destination while we update it */
	ioapic_write(idx, IOAPIC_REG_REDTBL + pin * 2 + 1, (uint32_t)(value >> 32));
	ioapic_write(idx, IOAPIC_REG_REDTBL + pin * 2, (uint32_t)value);
}

/* find which I/O APIC (and which of its pins) owns a given GSI, using each
 * controller's gsi_base and its IOAPICVER-reported redirection entry count */
static size_t ioapic_index_for_gsi(uint32_t gsi, uint8_t *pin_out)
{
	for (size_t i = 0; i < ioapic_count; i++) {
		uint32_t ver = ioapic_read(i, IOAPIC_REG_VER);
		uint32_t max_pin = (ver >> 16) & 0xFF;

		if (gsi >= ioapic[i].gsi_base && gsi <= ioapic[i].gsi_base + max_pin) {
			*pin_out = (uint8_t)(gsi - ioapic[i].gsi_base);
			return i;
		}
	}

	panic(NULL, "apic: no ioapic owns gsi=%u", gsi);
	__builtin_unreachable();
}

void ioapic_init(void)
{
	if (ioapic_count == 0) {
		log("apic: warning: no IOAPIC entries in MADT\n");
		return;
	}

	for (size_t i = 0; i < ioapic_count; i++) {
		ioapic_mmio[i] =
		    (volatile uint32_t *)vmap_mmio(kernel_vctx, ioapic[i].address, PAGE_SIZE);

		uint32_t ver = ioapic_read(i, IOAPIC_REG_VER);
		uint32_t max_pin = (ver >> 16) & 0xFF;

		log("apic: ioapic[%u] madt_id=%u gsi_base=%u entries=%u phys=0x%x\n",
		       (unsigned)i,
		       ioapic[i].id,
		       ioapic[i].gsi_base,
		       max_pin + 1,
		       ioapic[i].address);

		/* mask every redirection entry until a driver explicitly routes
		 * and unmasks it, to avoid spurious interrupts on unconfigured
		 * vectors */
		for (uint32_t pin = 0; pin <= max_pin; pin++)
			ioapic_write_redir(i, (uint8_t)pin, IOAPIC_REDTBL_MASKED);
	}
}

void ioapic_mask_gsi(uint32_t gsi)
{
	uint8_t pin;
	size_t idx = ioapic_index_for_gsi(gsi, &pin);
	uint64_t entry = ioapic_read_redir(idx, pin);
	ioapic_write_redir(idx, pin, entry | IOAPIC_REDTBL_MASKED);
}

void ioapic_unmask_gsi(uint32_t gsi)
{
	uint8_t pin;
	size_t idx = ioapic_index_for_gsi(gsi, &pin);
	uint64_t entry = ioapic_read_redir(idx, pin);
	ioapic_write_redir(idx, pin, entry & ~IOAPIC_REDTBL_MASKED);
}

void ioapic_set_irq(uint8_t irq, uint32_t lapic_id, uint8_t vector)
{
	uint32_t gsi = irq_resolve_gsi(irq);
	uint16_t flags = 0;

	/* apply any MADT Interrupt Source Override flags for this ISA IRQ */
	for (size_t i = 0; i < iso_count; i++) {
		if (iso[i].source == irq) {
			flags = iso[i].flags;
			break;
		}
	}

	uint64_t entry = vector;

	if ((flags & ACPI_MADT_POLARITY_MASK) == ACPI_MADT_POLARITY_ACTIVE_LOW)
		entry |= IOAPIC_REDTBL_ACTIVE_LOW;

	if ((flags & ACPI_MADT_TRIGGERING_MASK) == ACPI_MADT_TRIGGERING_LEVEL)
		entry |= IOAPIC_REDTBL_LEVEL;

	entry |= IOAPIC_REDTBL_MASKED;       /* stay masked until a handler is ready */
	entry |= ((uint64_t)lapic_id << 56); /* physical destination mode */

	uint8_t pin;
	size_t idx = ioapic_index_for_gsi(gsi, &pin);
	ioapic_write_redir(idx, pin, entry);

	log("apic: ioapic: irq%u -> gsi%u -> vector=0x%x dest=%u flags=0x%x\n",
	       irq,
	       gsi,
	       vector,
	       lapic_id,
	       flags);
}