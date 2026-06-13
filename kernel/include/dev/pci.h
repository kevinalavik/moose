#ifndef DEV_PCI_H
#define DEV_PCI_H

#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

struct pci_addr {
	uint16_t segment;
	uint8_t bus;
	uint8_t device;
	uint8_t function;
};

uint8_t pci_config_read8(struct pci_addr addr, uint16_t offset);
uint16_t pci_config_read16(struct pci_addr addr, uint16_t offset);
uint32_t pci_config_read32(struct pci_addr addr, uint16_t offset);

void pci_config_write8(struct pci_addr addr, uint16_t offset, uint8_t val);
void pci_config_write16(struct pci_addr addr, uint16_t offset, uint16_t val);
void pci_config_write32(struct pci_addr addr, uint16_t offset, uint32_t val);

void pci_scan(void);

#endif // DEV_PCI_H