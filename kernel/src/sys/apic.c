#include <sys/apic.h>
#include <uacpi/tables.h>
#include <uacpi/acpi.h>
#include <lib/printk.h>
#include <sys/panic.h>

#define MAX_LAPIC 256
#define MAX_IOAPIC 16
#define MAX_ISO 256
#define MAX_NMI 16
#define MAX_LAPIC_NMI 32

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

		if (lapic_count < MAX_LAPIC)
			lapic[lapic_count++] = *e;

		printk("apic: madt: LAPIC\t\t[%u] uid=%u apic_id=%u flags=0x%x\n",
		       lapic_count - 1,
		       e->uid,
		       e->id,
		       e->flags);
		break;
	}

	case ACPI_MADT_ENTRY_TYPE_IOAPIC: {
		struct acpi_madt_ioapic *e = (void *)hdr;

		if (ioapic_count < MAX_IOAPIC)
			ioapic[ioapic_count++] = *e;

		printk("apic: madt: IOAPIC\t\t[%u] id=%u addr=0x%x gsi_base=%u\n",
		       ioapic_count - 1,
		       e->id,
		       e->address,
		       e->gsi_base);
		break;
	}

	case ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE: {
		struct acpi_madt_interrupt_source_override *e = (void *)hdr;

		if (iso_count < MAX_ISO)
			iso[iso_count++] = *e;

		printk("apic: madt: ISO\t\t\t[%u] bus=%u irq=%02u->gsi=%02u flags=0x%x\n",
		       iso_count - 1,
		       e->bus,
		       e->source,
		       e->gsi,
		       e->flags);
		break;
	}

	case ACPI_MADT_ENTRY_TYPE_NMI_SOURCE: {
		struct acpi_madt_nmi_source *e = (void *)hdr;

		if (nmi_count < MAX_NMI)
			nmi[nmi_count++] = *e;

		printk(
		    "apic: madt: NMI\t[%u] gsi=%u flags=0x%x\n", nmi_count - 1, e->gsi, e->flags);
		break;
	}

	case ACPI_MADT_ENTRY_TYPE_LAPIC_NMI: {
		struct acpi_madt_lapic_nmi *e = (void *)hdr;

		if (lapic_nmi_count < MAX_LAPIC_NMI)
			lapic_nmi[lapic_nmi_count++] = *e;

		printk("apic: madt: APIC-NMI\t[%u] uid=%u lint=%u flags=0x%x\n",
		       lapic_nmi_count - 1,
		       e->uid,
		       e->lint,
		       e->flags);
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

void apic_init()
{
	setup_madt();
}