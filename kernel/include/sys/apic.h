#ifndef SYS_APIC_H
#define SYS_APIC_H

#include <stddef.h>
#include <stdint.h>
#include <uacpi/acpi.h>

/* ------------------------------------------------------------------------ */
/* MADT (Multiple APIC Description Table) parsing results                    */
/* ------------------------------------------------------------------------ */

#define MAX_LAPIC 256
#define MAX_IOAPIC 16
#define MAX_ISO 256
#define MAX_NMI 16
#define MAX_LAPIC_NMI 32

extern struct acpi_madt *madt;

extern struct acpi_madt_lapic lapic[MAX_LAPIC];
extern struct acpi_madt_ioapic ioapic[MAX_IOAPIC];
extern struct acpi_madt_interrupt_source_override iso[MAX_ISO];
extern struct acpi_madt_lapic_nmi lapic_nmi[MAX_LAPIC_NMI];

extern size_t lapic_count;
extern size_t ioapic_count;
extern size_t iso_count;
extern size_t lapic_nmi_count;

extern struct acpi_madt_lapic_address_override lapic_override;
extern int has_lapic_override;

void setup_madt(void);

/* ------------------------------------------------------------------------ */
/* Local APIC                                                                 */
/* ------------------------------------------------------------------------ */

/* register offsets (xAPIC MMIO offsets; x2APIC MSR = 0x800 + offset/16) */
#define LAPIC_REG_ID 0x020
#define LAPIC_REG_VERSION 0x030
#define LAPIC_REG_TPR 0x080
#define LAPIC_REG_EOI 0x0B0
#define LAPIC_REG_LDR 0x0D0
#define LAPIC_REG_DFR 0x0E0
#define LAPIC_REG_SVR 0x0F0
#define LAPIC_REG_ICR_LOW 0x300
#define LAPIC_REG_ICR_HIGH 0x310
#define LAPIC_REG_LVT_TIMER 0x320
#define LAPIC_REG_LVT_THERMAL 0x330
#define LAPIC_REG_LVT_PERF 0x340
#define LAPIC_REG_LVT_LINT0 0x350
#define LAPIC_REG_LVT_LINT1 0x360
#define LAPIC_REG_LVT_ERROR 0x370
#define LAPIC_REG_TIMER_INIT 0x380
#define LAPIC_REG_TIMER_CUR 0x390
#define LAPIC_REG_TIMER_DIV 0x3E0

#define LAPIC_SVR_ENABLE (1u << 8)
#define LAPIC_LVT_MASKED (1u << 16)
#define LAPIC_LVT_NMI (0x4u << 8) /* delivery mode = NMI */

#define LAPIC_SPURIOUS_VECTOR 0xFF

void lapic_init(void);
uint32_t lapic_read(uint32_t reg);
void lapic_write(uint32_t reg, uint32_t value);
void lapic_eoi(void);
uint32_t lapic_get_id(void);
void lapic_send_ipi(uint32_t apic_id, uint8_t vector);

/* ------------------------------------------------------------------------ */
/* I/O APIC                                                                   */
/* ------------------------------------------------------------------------ */

#define IOAPIC_REG_ID 0x00
#define IOAPIC_REG_VER 0x01
#define IOAPIC_REG_ARB 0x02
#define IOAPIC_REG_REDTBL 0x10

#define IOAPIC_REDTBL_MASKED (1ULL << 16)
#define IOAPIC_REDTBL_ACTIVE_LOW (1ULL << 13)
#define IOAPIC_REDTBL_LEVEL (1ULL << 15)

void ioapic_init(void);
uint32_t ioapic_read(size_t idx, uint8_t reg);
void ioapic_write(size_t idx, uint8_t reg, uint32_t value);

/* route a legacy ISA IRQ (0-15) to `vector` on the given LAPIC id, applying
 * any MADT Interrupt Source Override for that IRQ. the entry is left masked;
 * call ioapic_unmask_gsi() once a handler for `vector` has been installed. */
void ioapic_set_irq(uint8_t irq, uint32_t lapic_id, uint8_t vector);
void ioapic_mask_gsi(uint32_t gsi);
void ioapic_unmask_gsi(uint32_t gsi);

/* ------------------------------------------------------------------------ */
/* top level                                                                  */
/* ------------------------------------------------------------------------ */

/* legacy ISA IRQs (0-15) are routed starting at this vector */
#define IRQ_VECTOR_BASE 0x20

void pic_disable(void);
void apic_init(void);
uint32_t irq_resolve_gsi(uint8_t irq);

#endif // SYS_APIC_H