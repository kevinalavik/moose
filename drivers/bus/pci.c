#include <bus/pci.h>
#include <arch/cpu.h>

static uint32_t pci_make_addr(pci_addr_t a, size_t offset)
{
	return (uint32_t)(1UL << 31) | ((uint32_t)a.bus << 16) |
	       ((uint32_t)a.device << 11) | ((uint32_t)a.function << 8) |
	       (uint32_t)(offset & 0xFC);
}

uint8_t pci_config_read8(pci_addr_t addr, size_t offset)
{
	outl(PCI_CONFIG_ADDR, pci_make_addr(addr, offset));
	return (uint8_t)(inl(PCI_CONFIG_DATA) >> ((offset & 3) * 8));
}

uint16_t pci_config_read16(pci_addr_t addr, size_t offset)
{
	outl(PCI_CONFIG_ADDR, pci_make_addr(addr, offset));
	return (uint16_t)(inl(PCI_CONFIG_DATA) >> ((offset & 2) * 8));
}

uint32_t pci_config_read32(pci_addr_t addr, size_t offset)
{
	outl(PCI_CONFIG_ADDR, pci_make_addr(addr, offset));
	return inl(PCI_CONFIG_DATA);
}

void pci_config_write8(pci_addr_t addr, size_t offset, uint8_t val)
{
	uint32_t addr_reg = pci_make_addr(addr, offset);
	outl(PCI_CONFIG_ADDR, addr_reg);
	uint32_t data = inl(PCI_CONFIG_DATA);
	uint32_t shift = (offset & 3) * 8;
	data &= ~(0xFFU << shift);
	data |= ((uint32_t)val << shift);
	outl(PCI_CONFIG_ADDR, addr_reg);
	outl(PCI_CONFIG_DATA, data);
}

void pci_config_write16(pci_addr_t addr, size_t offset, uint16_t val)
{
	uint32_t addr_reg = pci_make_addr(addr, offset);
	outl(PCI_CONFIG_ADDR, addr_reg);
	uint32_t data = inl(PCI_CONFIG_DATA);
	uint32_t shift = (offset & 2) * 8;
	data &= ~(0xFFFFU << shift);
	data |= ((uint32_t)val << shift);
	outl(PCI_CONFIG_ADDR, addr_reg);
	outl(PCI_CONFIG_DATA, data);
}

void pci_config_write32(pci_addr_t addr, size_t offset, uint32_t val)
{
	outl(PCI_CONFIG_ADDR, pci_make_addr(addr, offset));
	outl(PCI_CONFIG_DATA, val);
}
