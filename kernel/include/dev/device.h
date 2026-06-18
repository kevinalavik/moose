#ifndef DEV_DEVICE_H
#define DEV_DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/spinlock.h>

typedef struct device device_t;
typedef struct driver driver_t;
typedef struct bus_type bus_type_t;
typedef struct device_id device_id_t;

struct device_id {
	const char *id;
	uintptr_t data;
};

struct driver {
	const char *name;

	const device_id_t *id_table;

	int (*probe)(device_t *dev, const device_id_t *id);
	void (*remove)(device_t *dev);

	driver_t *next;
};

struct device {
	const char *name;
	bus_type_t *bus;
	driver_t *driver;
	device_t *parent;

	void *bus_data;
	void *driver_data;

	spinlock_t lock;
	device_t *next;
};

struct bus_type {
	const char *name;

	bool (*match)(device_t *dev, driver_t *drv);
	int (*probe)(device_t *dev, driver_t *drv);

	driver_t *drivers;
	device_t *devices;
	spinlock_t lock;
	bus_type_t *next;
};

void bus_register(bus_type_t *bus);
void driver_register(bus_type_t *bus, driver_t *drv);

device_t *device_create(bus_type_t *bus, device_t *parent, const char *name, void *bus_data);
void device_destroy(device_t *dev);

static inline void *device_get_data(const device_t *dev)
{
	return dev->driver_data;
}

static inline void device_set_data(device_t *dev, void *data)
{
	dev->driver_data = data;
}

int bus_for_each_device(bus_type_t *bus, int (*cb)(device_t *dev, void *user), void *user);
void device_model_init(void);

#endif // DEV_DEVICE_H