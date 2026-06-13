#include <dev/pci.h>
#include <arch/io.h>
#include <lib/printk.h>

static uint32_t pci_make_address(struct pci_addr addr, uint16_t offset)
{
	return (uint32_t)((addr.bus << 16) | (addr.device << 11) | (addr.function << 8) |
	                  (offset & 0xFC) | 0x80000000);
}

uint8_t pci_config_read8(struct pci_addr addr, uint16_t offset)
{
	outl(PCI_CONFIG_ADDRESS, pci_make_address(addr, offset));
	return inb(PCI_CONFIG_DATA + (offset & 3));
}

uint16_t pci_config_read16(struct pci_addr addr, uint16_t offset)
{
	outl(PCI_CONFIG_ADDRESS, pci_make_address(addr, offset));
	return inw(PCI_CONFIG_DATA + (offset & 2));
}

uint32_t pci_config_read32(struct pci_addr addr, uint16_t offset)
{
	outl(PCI_CONFIG_ADDRESS, pci_make_address(addr, offset));
	return inl(PCI_CONFIG_DATA);
}

void pci_config_write8(struct pci_addr addr, uint16_t offset, uint8_t val)
{
	outl(PCI_CONFIG_ADDRESS, pci_make_address(addr, offset));
	outb(PCI_CONFIG_DATA + (offset & 3), val);
}

void pci_config_write16(struct pci_addr addr, uint16_t offset, uint16_t val)
{
	outl(PCI_CONFIG_ADDRESS, pci_make_address(addr, offset));
	outw(PCI_CONFIG_DATA + (offset & 2), val);
}

void pci_config_write32(struct pci_addr addr, uint16_t offset, uint32_t val)
{
	outl(PCI_CONFIG_ADDRESS, pci_make_address(addr, offset));
	outl(PCI_CONFIG_DATA, val);
}

static const char *pci_class_name(uint8_t class_code, uint8_t subclass)
{
	switch (class_code) {
	case 0x00:
		return "Unclassified";
	case 0x01:
		return "Mass Storage Controller";
	case 0x02:
		return "Network Controller";
	case 0x03:
		return "Display Controller";
	case 0x04:
		return "Multimedia Controller";
	case 0x05:
		return "Memory Controller";
	case 0x06:
		switch (subclass) {
		case 0x00:
			return "Host Bridge";
		case 0x01:
			return "ISA Bridge";
		case 0x04:
			return "PCI-to-PCI Bridge";
		default:
			return "Bridge";
		}
	case 0x07:
		return "Communication Controller";
	case 0x08:
		return "System Peripheral";
	case 0x09:
		return "Input Device Controller";
	case 0x0A:
		return "Docking Station";
	case 0x0B:
		return "Processor";
	case 0x0C:
		switch (subclass) {
		case 0x03:
			return "USB Controller";
		case 0x05:
			return "SMBus Controller";
		default:
			return "Serial Bus Controller";
		}
	case 0x0D:
		return "Wireless Controller";
	default:
		return "Unknown";
	}
}

static void pci_scan_function(uint8_t bus, uint8_t device, uint8_t function)
{
	struct pci_addr addr = {
	    .segment = 0,
	    .bus = bus,
	    .device = device,
	    .function = function,
	};

	uint16_t vendor_id = pci_config_read16(addr, 0x00);
	if (vendor_id == 0xFFFF)
		return;

	uint16_t device_id = pci_config_read16(addr, 0x02);
	uint8_t class_code = pci_config_read8(addr, 0x0B);
	uint8_t subclass = pci_config_read8(addr, 0x0A);
	uint8_t prog_if = pci_config_read8(addr, 0x09);
	uint8_t revision = pci_config_read8(addr, 0x08);

	printk("pci: %02x:%02x.%x %04x:%04x class %02x.%02x.%02x rev %02x - %s\n",
	       bus,
	       device,
	       function,
	       vendor_id,
	       device_id,
	       class_code,
	       subclass,
	       prog_if,
	       revision,
	       pci_class_name(class_code, subclass));
}

void pci_scan(void)
{
	for (uint32_t bus = 0; bus < 256; bus++) {
		for (uint32_t device = 0; device < 32; device++) {
			struct pci_addr addr = {
			    .segment = 0,
			    .bus = (uint8_t)bus,
			    .device = (uint8_t)device,
			    .function = 0,
			};

			if (pci_config_read16(addr, 0x00) == 0xFFFF)
				continue;

			uint8_t header_type = pci_config_read8(addr, 0x0E);
			uint8_t nfuncs = (header_type & 0x80) ? 8 : 1;

			for (uint8_t function = 0; function < nfuncs; function++)
				pci_scan_function((uint8_t)bus, (uint8_t)device, function);
		}
	}
}