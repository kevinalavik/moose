#ifndef DEVICE_DEVICE_H
#define DEVICE_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IORESOURCE_IO 0x00000100
#define IORESOURCE_MEM 0x00000200
#define IORESOURCE_IRQ 0x00000400
#define IORESOURCE_DMA 0x00000800
#define DEVICE_MAX_RESOURCES 8

typedef struct resource {
	const char *name;
	uintptr_t start;
	uintptr_t end;
	unsigned long flags;
} resource_t;

typedef struct char_dev {
	void *private_data;
	size_t (*read)(struct char_dev *dev, void *buf, size_t len);
	size_t (*write)(struct char_dev *dev, const void *buf, size_t len);
	const char *name;
} char_dev_t;

#define CHAR_DEV_NULL                        \
	((char_dev_t){ .private_data = NULL, \
		       .read = NULL,         \
		       .write = NULL,        \
		       .name = NULL })

static inline bool char_dev_valid(char_dev_t *d)
{
	return d && d->private_data && d->read && d->write;
}

typedef struct device_driver driver_t;
typedef struct device device_t;
typedef struct bus_type bus_type_t;
typedef int (*driver_initcall_t)(void);

typedef int (*bus_match_fn)(device_t *dev, driver_t *drv);

struct bus_type {
	const char *name;
	bus_match_fn match;
	device_t **devices;
	driver_t **drivers;
	size_t num_devices;
	size_t num_drivers;
	size_t cap_devices;
	size_t cap_drivers;
};

struct device {
	const char *name;
	bus_type_t *bus;
	device_t *parent;
	driver_t *driver;
	resource_t resources[DEVICE_MAX_RESOURCES];
	size_t num_resources;
	void *driver_data;
	char_dev_t chardev;
};

struct device_driver {
	const char *name;
	bus_type_t *bus;
	int (*probe)(device_t *dev);
	int (*remove)(device_t *dev);
};

int bus_register(bus_type_t *bus);
int device_register(device_t *dev);
int driver_register(driver_t *drv);
int device_add_resource(device_t *dev, resource_t *r);
int bus_rescan_devices(void);
int driver_init_all(void);

#define DRIVER_INIT(fn)                             \
	static driver_initcall_t __driver_init_##fn \
		__attribute__((used, section(".driver_init"))) = fn

static inline void dev_set_drvdata(device_t *dev, void *data)
{
	dev->driver_data = data;
}

static inline void *dev_get_drvdata(device_t *dev)
{
	return dev->driver_data;
}

#define device_for_each_res(dev, i, r)                                  \
	for (i = 0, r = &(dev)->resources[0]; i < (dev)->num_resources; \
	     i++, r = &(dev)->resources[i])

#endif /* DEVICE_DEVICE_H */
