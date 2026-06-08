#ifndef ARCH_APIC_H
#define ARCH_APIC_H

#include <stdint.h>

#define IRQ_BASE 0x20

int apic_init(void);
void apic_eoi(void);
int apic_gsi_set_mask(uint32_t gsi, int masked);

#endif /* ARCH_APIC_H */
