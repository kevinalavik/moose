#ifndef ARCH_ACPI_H
#define ARCH_ACPI_H

#include <stdint.h>
#include <stddef.h>
#include <uacpi/acpi.h>

int acpi_init(void);

/* MADT parsed data exposed for apic.c */
extern struct acpi_madt *madt;

extern struct acpi_madt_lapic lapic[];
extern size_t lapic_count;

extern struct acpi_madt_ioapic ioapic[];
extern size_t ioapic_count;

extern struct acpi_madt_interrupt_source_override iso[];
extern size_t iso_count;

extern struct acpi_madt_lapic_nmi lapic_nmi[];
extern size_t lapic_nmi_count;

extern struct acpi_madt_lapic_address_override lapic_override;
extern int has_lapic_override;

#endif /* ARCH_ACPI_H*/