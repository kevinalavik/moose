#ifndef DEVICE_PLATFORM_H
#define DEVICE_PLATFORM_H

#include <device/device.h>

typedef struct platform_device_id {
	const char *name;
} platform_device_id_t;

typedef struct platform_driver {
	driver_t driver;
	const platform_device_id_t *id_table;
} platform_driver_t;

extern bus_type_t platform_bus;

int platform_driver_register(platform_driver_t *pdrv);
int platform_device_add_res_io(device_t *dev, uint16_t port_base,
			       uint16_t port_end);
int platform_device_add_res_irq(device_t *dev, uint32_t irq);
int platform_device_add_res_mem(device_t *dev, uintptr_t start, uintptr_t end);
int acpi_platform_scan(void);

#endif /* DEVICE_PLATFORM_H */
