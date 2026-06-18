#ifndef DEV_ACPI_BUS_H
#define DEV_ACPI_BUS_H

#include <dev/device.h>
#include <uacpi/namespace.h>
#include <uacpi/utilities.h>

typedef struct acpi_device_info {
	uacpi_namespace_node *node;
	uacpi_namespace_node_info *info;
} acpi_device_info_t;

extern bus_type_t acpi_bus;

static inline uacpi_namespace_node *acpi_dev_node(const device_t *dev)
{
	const acpi_device_info_t *ai = (const acpi_device_info_t *)dev->bus_data;
	return ai ? ai->node : (uacpi_namespace_node *)0;
}

static inline const uacpi_namespace_node_info *acpi_dev_info(const device_t *dev)
{
	const acpi_device_info_t *ai = (const acpi_device_info_t *)dev->bus_data;
	return ai ? ai->info : (const uacpi_namespace_node_info *)0;
}

void acpi_bus_init(void);
void acpi_bus_enumerate(void);

#endif // DEV_ACPI_BUS_H