#include <device/platform.h>
#include <sys/klog.h>
#include <lib/string.h>

static int platform_match(device_t *dev, driver_t *drv)
{
	uintptr_t base = (uintptr_t)((platform_driver_t *)drv)->id_table;
	if (base) {
		const platform_device_id_t *id =
			(const platform_device_id_t *)base;
		while (id->name) {
			if (strcmp(dev->name, id->name) == 0)
				return 1;
			id++;
		}
		return 0;
	}
	return strcmp(dev->name, drv->name) == 0;
}

bus_type_t platform_bus = {
	.name = "platform",
	.match = platform_match,
};

int platform_driver_register(platform_driver_t *pdrv)
{
	pdrv->driver.bus = &platform_bus;
	return driver_register(&pdrv->driver);
}

int platform_device_add_res_io(device_t *dev, uint16_t port_base,
			       uint16_t port_end)
{
	resource_t r = {
		.name = "io",
		.start = port_base,
		.end = port_end,
		.flags = IORESOURCE_IO,
	};
	return device_add_resource(dev, &r);
}

int platform_device_add_res_irq(device_t *dev, uint32_t irq)
{
	resource_t r = {
		.name = "irq",
		.start = irq,
		.end = irq,
		.flags = IORESOURCE_IRQ,
	};
	return device_add_resource(dev, &r);
}

int platform_device_add_res_mem(device_t *dev, uintptr_t start, uintptr_t end)
{
	resource_t r = {
		.name = "mem",
		.start = start,
		.end = end,
		.flags = IORESOURCE_MEM,
	};
	return device_add_resource(dev, &r);
}
