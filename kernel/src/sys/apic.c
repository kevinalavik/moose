#include <sys/apic.h>
#include <uacpi/tables.h>
#include <uacpi/acpi.h>
#include <lib/printk.h>
#include <sys/panic.h>
#include <arch/io.h>

struct acpi_madt *madt;

struct acpi_madt_lapic lapic[MAX_LAPIC];
struct acpi_madt_ioapic ioapic[MAX_IOAPIC];
struct acpi_madt_interrupt_source_override iso[MAX_ISO];
static struct acpi_madt_nmi_source nmi[MAX_NMI];
struct acpi_madt_lapic_nmi lapic_nmi[MAX_LAPIC_NMI];

size_t lapic_count;
size_t ioapic_count;
size_t iso_count;
static size_t nmi_count;
size_t lapic_nmi_count;

struct acpi_madt_lapic_address_override lapic_override;
int has_lapic_override;

static uacpi_iteration_decision parse_madt(void *user, struct acpi_entry_hdr *hdr)
{
	(void)user;

	switch (hdr->type) {
	case ACPI_MADT_ENTRY_TYPE_LAPIC: {
		struct acpi_madt_lapic *e = (void *)hdr;

		if (lapic_count >= MAX_LAPIC) {
			printk("apic: madt: LAPIC: too many entries, dropping uid=%u apic_id=%u\n",
			       e->uid,
			       e->id);
			break;
		}

		lapic[lapic_count] = *e;
		printk("apic: madt: LAPIC\t\t[%u] uid=%u apic_id=%u flags=0x%x\n",
		       lapic_count,
		       e->uid,
		       e->id,
		       e->flags);
		lapic_count++;
		break;
	}

	case ACPI_MADT_ENTRY_TYPE_IOAPIC: {
		struct acpi_madt_ioapic *e = (void *)hdr;

		if (ioapic_count >= MAX_IOAPIC) {
			printk("apic: madt: IOAPIC: too many entries, dropping id=%u addr=0x%x\n",
			       e->id,
			       e->address);
			break;
		}

		ioapic[ioapic_count] = *e;
		printk("apic: madt: IOAPIC\t\t[%u] id=%u addr=0x%x gsi_base=%u\n",
		       ioapic_count,
		       e->id,
		       e->address,
		       e->gsi_base);
		ioapic_count++;
		break;
	}

	case ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE: {
		struct acpi_madt_interrupt_source_override *e = (void *)hdr;

		if (iso_count >= MAX_ISO) {
			printk("apic: madt: ISO: too many entries, dropping irq=%02u->gsi=%02u\n",
			       e->source,
			       e->gsi);
			break;
		}

		iso[iso_count] = *e;
		printk("apic: madt: ISO\t\t\t[%u] bus=%u irq=%02u->gsi=%02u flags=0x%x\n",
		       iso_count,
		       e->bus,
		       e->source,
		       e->gsi,
		       e->flags);
		iso_count++;
		break;
	}

	case ACPI_MADT_ENTRY_TYPE_NMI_SOURCE: {
		struct acpi_madt_nmi_source *e = (void *)hdr;

		if (nmi_count >= MAX_NMI) {
			printk("apic: madt: NMI: too many entries, dropping gsi=%u\n", e->gsi);
			break;
		}

		nmi[nmi_count] = *e;
		printk("apic: madt: NMI\t[%u] gsi=%u flags=0x%x\n", nmi_count, e->gsi, e->flags);
		nmi_count++;
		break;
	}

	case ACPI_MADT_ENTRY_TYPE_LAPIC_NMI: {
		struct acpi_madt_lapic_nmi *e = (void *)hdr;

		if (lapic_nmi_count >= MAX_LAPIC_NMI) {
			printk("apic: madt: APIC-NMI: too many entries, dropping uid=%u lint=%u\n",
			       e->uid,
			       e->lint);
			break;
		}

		lapic_nmi[lapic_nmi_count] = *e;
		printk("apic: madt: APIC-NMI\t[%u] uid=%u lint=%u flags=0x%x\n",
		       lapic_nmi_count,
		       e->uid,
		       e->lint,
		       e->flags);
		lapic_nmi_count++;
		break;
	}

	case ACPI_MADT_ENTRY_TYPE_LAPIC_ADDRESS_OVERRIDE: {
		struct acpi_madt_lapic_address_override *e = (void *)hdr;

		lapic_override = *e;
		has_lapic_override = 1;

		printk("apic: madt: LAPIC override: 0x%llx\n", (unsigned long long)e->address);
		break;
	}

	default:
		printk("apic: madt: unknown entry type=%u len=%u\n", hdr->type, hdr->length);
		break;
	}

	return UACPI_ITERATION_DECISION_CONTINUE;
}

void setup_madt()
{
	uacpi_table madt_table;
	uacpi_status status;

	status = uacpi_table_find_by_signature(ACPI_MADT_SIGNATURE, &madt_table);
	if (uacpi_unlikely_error(status)) {
		panic(NULL, "apic: table lookup failed: %s", uacpi_status_to_string(status));
		return;
	}

	madt = (struct acpi_madt *)madt_table.ptr;
	if (!madt) {
		panic(NULL, "failed to get MADT table: null pointer");
		return;
	}

	printk("apic: lapic base: 0x%llx flags=0x%x\n",
	       (unsigned long long)madt->local_interrupt_controller_address,
	       madt->flags);

	status = uacpi_for_each_subtable(&madt->hdr, sizeof(struct acpi_madt), parse_madt, NULL);
	if (uacpi_unlikely_error(status)) {
		panic(NULL, "apic: table lookup failed: %s", uacpi_status_to_string(status));
		return;
	}
}

/* resolve the GSI for a legacy ISA IRQ, applying any MADT Interrupt Source
 * Override. returns the GSI (which may be shared by multiple IRQs). */
uint32_t irq_resolve_gsi(uint8_t irq)
{
	uint32_t gsi = irq;
	for (size_t i = 0; i < iso_count; i++) {
		if (iso[i].source == irq) {
			gsi = iso[i].gsi;
			break;
		}
	}
	return gsi;
}

/* legacy 8259 PIC ports */
#define PIC1_CMD 0x20
#define PIC2_CMD 0xA0
#define PIC1_DATA 0x21
#define PIC2_DATA 0xA1

/* IMCR (Interrupt Mode Configuration Register), present on chipsets older
 * than ICH. Writing this switches interrupt delivery away from the legacy
 * PIC and over to the APIC. it's a no-op (but harmless) on chipsets that
 * don't implement it. */
#define IMCR_ADDR 0x22
#define IMCR_DATA 0x23

void pic_disable(void)
{
	/* mask every line on both legacy 8259 PICs */
	outb(PIC1_DATA, 0xFF);
	outb(PIC2_DATA, 0xFF);

	/* select IMCR, route interrupts via the APIC */
	outb(IMCR_ADDR, 0x70);
	outb(IMCR_DATA, 0x01);

	printk("apic: legacy PIC masked, IMCR set to APIC mode\n");
}

void apic_init()
{
	setup_madt();
	pic_disable();

	lapic_init();
	ioapic_init();

	uint32_t bsp_id = lapic_get_id();
	printk("apic: bootstrap CPU lapic id = %u\n", bsp_id);

	/* route the legacy ISA IRQs (0-15) to the bootstrap CPU. each entry
	 * stays masked (see ioapic_set_irq) until a driver installs a handler
	 * for its vector and calls ioapic_unmask_gsi(). */
	if (ioapic_count == 0) {
		printk("apic: no IOAPIC present, skipping legacy IRQ routing\n");
		return;
	}

	/* track which GSIs have been mapped to avoid aliasing conflicts
	 * (e.g. ISA IRQ0 and IRQ2 can both resolve to GSI2) */
	bool gsi_mapped[256] = {false};

	for (uint8_t irq = 0; irq < 16; irq++) {
		uint32_t gsi = irq_resolve_gsi(irq);
		if (gsi_mapped[gsi]) {
			printk("apic: ioapic: irq%u -> gsi%u already routed, skipping\n", irq, gsi);
			continue;
		}
		gsi_mapped[gsi] = true;
		ioapic_set_irq(irq, bsp_id, (uint8_t)(IRQ_VECTOR_BASE + irq));
	}
}