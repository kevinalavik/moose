#ifndef DEV_PCI_H
#define DEV_PCI_H

#include <stddef.h>
#include <stdint.h>

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

typedef struct {
	uint16_t segment;
	uint8_t bus;
	uint8_t device;
	uint8_t function;
} pci_addr_t;

uint8_t pci_config_read8(pci_addr_t addr, size_t offset);
uint16_t pci_config_read16(pci_addr_t addr, size_t offset);
uint32_t pci_config_read32(pci_addr_t addr, size_t offset);
void pci_config_write8(pci_addr_t addr, size_t offset, uint8_t val);
void pci_config_write16(pci_addr_t addr, size_t offset, uint16_t val);
void pci_config_write32(pci_addr_t addr, size_t offset, uint32_t val);

#endif
