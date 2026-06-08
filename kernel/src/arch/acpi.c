#include <mm/kheap.h>
#include <arch/acpi.h>
#include <lib/string.h>
#include <uacpi/uacpi.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include <uacpi/event.h>
#include <sys/errno.h>
#include <sys/klog.h>

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

static uacpi_iteration_decision parse_madt(void *user,
					   struct acpi_entry_hdr *hdr)
{
	(void)user;

	switch (hdr->type) {
	case ACPI_MADT_ENTRY_TYPE_LAPIC: {
		struct acpi_madt_lapic *e = (void *)hdr;

		if (lapic_count < MAX_LAPIC)
			lapic[lapic_count++] = *e;

		klog("LAPIC", "\t[%zu] uid=%u apic_id=%u flags=0x%x",
		     lapic_count - 1, e->uid, e->id, e->flags);
		break;
	}

	case ACPI_MADT_ENTRY_TYPE_IOAPIC: {
		struct acpi_madt_ioapic *e = (void *)hdr;

		if (ioapic_count < MAX_IOAPIC)
			ioapic[ioapic_count++] = *e;

		klog("IOAPIC", "\t[%zu] id=%u addr=0x%x gsi_base=%u",
		     ioapic_count - 1, e->id, e->address, e->gsi_base);
		break;
	}

	case ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE: {
		struct acpi_madt_interrupt_source_override *e = (void *)hdr;

		if (iso_count < MAX_ISO)
			iso[iso_count++] = *e;

		klog("ISO", "\t[%zu] bus=%u irq=%02u -> gsi=%02u flags=0x%x",
		     iso_count - 1, e->bus, e->source, e->gsi, e->flags);
		break;
	}

	case ACPI_MADT_ENTRY_TYPE_NMI_SOURCE: {
		struct acpi_madt_nmi_source *e = (void *)hdr;

		if (nmi_count < MAX_NMI)
			nmi[nmi_count++] = *e;

		klog("NMI", "\t[%zu] gsi=%u flags=0x%x", nmi_count - 1, e->gsi,
		     e->flags);
		break;
	}

	case ACPI_MADT_ENTRY_TYPE_LAPIC_NMI: {
		struct acpi_madt_lapic_nmi *e = (void *)hdr;

		if (lapic_nmi_count < MAX_LAPIC_NMI)
			lapic_nmi[lapic_nmi_count++] = *e;

		klog("LAPIC-NMI", "\t[%zu] uid=%u lint=%u flags=0x%x",
		     lapic_nmi_count - 1, e->uid, e->lint, e->flags);
		break;
	}

	case ACPI_MADT_ENTRY_TYPE_LAPIC_ADDRESS_OVERRIDE: {
		struct acpi_madt_lapic_address_override *e = (void *)hdr;

		lapic_override = *e;
		has_lapic_override = 1;

		klog("MADT", "LAPIC override: 0x%llx",
		     (unsigned long long)e->address);
		break;
	}

	default:
		klog("MADT", "unknown entry type=%u len=%u", hdr->type,
		     hdr->length);
		break;
	}

	return UACPI_ITERATION_DECISION_CONTINUE;
}

static int madt_init(void)
{
	uacpi_table table;
	uacpi_status r;

	r = uacpi_table_find_by_signature(ACPI_MADT_SIGNATURE, &table);
	if (uacpi_unlikely_error(r)) {
		klog("MADT", "table lookup failed: %s",
		     uacpi_status_to_string(r));
		return -ENODEV;
	}

	madt = (struct acpi_madt *)table.ptr;
	if (!madt) {
		klog("MADT", "null pointer");
		return -ENODEV;
	}

	klog("MADT", "lapic base: 0x%llx flags=0x%x",
	     (unsigned long long)madt->local_interrupt_controller_address,
	     madt->flags);

	r = uacpi_for_each_subtable(&madt->hdr, sizeof(struct acpi_madt),
				    parse_madt, NULL);

	if (uacpi_unlikely_error(r)) {
		klog("MADT", "iteration failed: %s", uacpi_status_to_string(r));
		return -EIO;
	}

	klog("MADT", "summary: LAPIC=%zu IOAPIC=%zu ISO=%zu", lapic_count,
	     ioapic_count, iso_count);

	return 0;
}

int acpi_init(void)
{
	uacpi_status ret;

	ret = uacpi_initialize(0);
	if (uacpi_unlikely_error(ret)) {
		klog("uACPI", "init failed: %s", uacpi_status_to_string(ret));
		return -ENODEV;
	}

	ret = uacpi_namespace_load();
	if (uacpi_unlikely_error(ret)) {
		klog("uACPI", COL_RED "namespace load failed: %s" COL_RESET,
		     uacpi_status_to_string(ret));
		return -ENODEV;
	}

	ret = uacpi_namespace_initialize();
	if (uacpi_unlikely_error(ret)) {
		klog("uACPI", COL_RED "namespace init failed: %s" COL_RESET,
		     uacpi_status_to_string(ret));
		return -ENODEV;
	}

	ret = uacpi_finalize_gpe_initialization();
	if (uacpi_unlikely_error(ret)) {
		klog("uACPI", COL_RED "GPE initialization error: %s" COL_RESET,
		     uacpi_status_to_string(ret));
		return -ENODEV;
	}

	return madt_init();
}