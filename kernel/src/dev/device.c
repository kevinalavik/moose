#include <dev/device.h>
#include <dev/platform.h>
#include <fs/devfs.h>
#include <mm/kheap.h>
#include <sys/errno.h>
#include <sys/klog.h>
#include <lib/string.h>

#define MAX_BUSES 8
static bus_type_t *buses[MAX_BUSES];
static size_t bus_count;

static int array_grow(void ***arr, size_t *cap, size_t elem_size)
{
	size_t new_cap = *cap ? *cap * 2 : 8;
	void *tmp = kmalloc(new_cap * elem_size);
	if (!tmp)
		return -ENOMEM;
	if (*arr) {
		memcpy(tmp, *arr, *cap * elem_size);
		kfree(*arr);
	}
	*arr = tmp;
	*cap = new_cap;
	return 0;
}

static int array_push(void ***arr, size_t *cap, size_t *cnt, void *item,
		      size_t elem_size)
{
	if (*cnt >= *cap) {
		int ret = array_grow(arr, cap, elem_size);
		if (ret)
			return ret;
	}
	uintptr_t *base = (uintptr_t *)*arr;
	base[*cnt] = (uintptr_t)item;
	*cnt += 1;
	return 0;
}

int bus_register(bus_type_t *bus)
{
	if (!bus || !bus->name || !bus->match)
		return -EINVAL;
	if (bus_count >= MAX_BUSES)
		return -ENOMEM;
	bus->devices = NULL;
	bus->drivers = NULL;
	bus->num_devices = 0;
	bus->num_drivers = 0;
	bus->cap_devices = 0;
	bus->cap_drivers = 0;
	buses[bus_count++] = bus;
	klog("bus", "registered bus '%s'", bus->name);
	return 0;
}

int device_register(device_t *dev)
{
	if (!dev || !dev->name || !dev->bus)
		return -EINVAL;
	bus_type_t *bus = dev->bus;
	int ret = array_push((void ***)&bus->devices, &bus->cap_devices,
			     &bus->num_devices, dev, sizeof(void *));
	if (ret)
		return ret;
	for (size_t i = 0; i < bus->num_drivers; i++) {
		driver_t *drv = bus->drivers[i];
		if (bus->match(dev, drv)) {
			klog("bus", "device '%s' matches driver '%s'",
			     dev->name, drv->name);
			if (drv->probe && drv->probe(dev) == 0)
				dev->driver_data = drv;
			break;
		}
	}
	return 0;
}

int driver_register(driver_t *drv)
{
	if (!drv || !drv->name || !drv->bus)
		return -EINVAL;
	bus_type_t *bus = drv->bus;
	int ret = array_push((void ***)&bus->drivers, &bus->cap_drivers,
			     &bus->num_drivers, drv, sizeof(void *));
	if (ret)
		return ret;
	klog("bus", "driver '%s' registered on '%s'", drv->name, bus->name);
	for (size_t i = 0; i < bus->num_devices; i++) {
		device_t *dev = bus->devices[i];
		if (dev->driver_data)
			continue;
		if (bus->match(dev, drv)) {
			klog("bus", "driver '%s' matches device '%s'",
			     drv->name, dev->name);
			if (drv->probe && drv->probe(dev) == 0)
				dev->driver_data = drv;
			break;
		}
	}
	return 0;
}

int device_add_resource(device_t *dev, resource_t *r)
{
	if (!dev || !r)
		return -EINVAL;
	if (dev->num_resources >= DEVICE_MAX_RESOURCES)
		return -ENOMEM;
	dev->resources[dev->num_resources++] = *r;
	return 0;
}

int bus_probe_all(void)
{
	int total = 0;
	for (size_t b = 0; b < bus_count; b++) {
		bus_type_t *bus = buses[b];
		for (size_t i = 0; i < bus->num_devices; i++) {
			device_t *dev = bus->devices[i];
			if (dev->driver_data)
				continue;
			for (size_t j = 0; j < bus->num_drivers; j++) {
				driver_t *drv = bus->drivers[j];
				if (bus->match(dev, drv)) {
					klog("bus", "probing '%s' with '%s'",
					     dev->name, drv->name);
					if (drv->probe &&
					    drv->probe(dev) == 0) {
						dev->driver_data = drv;
						total++;
					}
					break;
				}
			}
		}
	}
	return total;
}

void device_register_chardevs(void)
{
	for (size_t b = 0; b < bus_count; b++) {
		bus_type_t *bus = buses[b];
		for (size_t i = 0; i < bus->num_devices; i++) {
			device_t *dev = bus->devices[i];
			if (char_dev_valid(&dev->chardev))
				devfs_register(dev->name, &dev->chardev);
		}
	}
}
