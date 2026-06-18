#include <dev/device.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/printk.h>
#include <sys/errno.h>
#include <sys/spinlock.h>

static bus_type_t *bus_list = NULL;
static spinlock_t bus_list_lock;

static int device_try_bind(device_t *dev, driver_t *drv)
{
	bus_type_t *bus = dev->bus;

	if (dev->driver)
		return -EBUSY;

	if (bus->match && !bus->match(dev, drv))
		return -ENODEV;

	int ret;
	if (bus->probe) {
		ret = bus->probe(dev, drv);
	} else if (drv->probe) {
		const device_id_t *matched_id = NULL;
		if (drv->id_table) {
			for (const device_id_t *id = drv->id_table; id->id; id++) {
				if (bus->match && bus->match(dev, drv)) {
					matched_id = id;
					break;
				}
			}
		}
		ret = drv->probe(dev, matched_id);
	} else {
		ret = 0;
	}

	if (ret == 0)
		dev->driver = drv;

	return ret;
}

void bus_register(bus_type_t *bus)
{
	if (!bus)
		return;

	spin_init(&bus->lock);
	bus->drivers = NULL;
	bus->devices = NULL;

	unsigned long flags = spin_lock_irqsave(&bus_list_lock);
	bus->next = bus_list;
	bus_list = bus;
	spin_unlock_irqrestore(&bus_list_lock, flags);

	log("device: registered bus '%s'\n", bus->name);
}

void driver_register(bus_type_t *bus, driver_t *drv)
{
	if (!bus || !drv)
		return;

	unsigned long flags = spin_lock_irqsave(&bus->lock);
	drv->next = bus->drivers;
	bus->drivers = drv;
	spin_unlock_irqrestore(&bus->lock, flags);
	log("device: registered driver '%s' on bus '%s'\n", drv->name, bus->name);

	flags = spin_lock_irqsave(&bus->lock);
	for (device_t *dev = bus->devices; dev; dev = dev->next) {
		if (!dev->driver) {
			spin_unlock_irqrestore(&bus->lock, flags);
			device_try_bind(dev, drv);
			flags = spin_lock_irqsave(&bus->lock);
		}
	}
	spin_unlock_irqrestore(&bus->lock, flags);
}

device_t *device_create(bus_type_t *bus, device_t *parent, const char *name, void *bus_data)
{
	if (!bus || !name)
		return NULL;

	device_t *dev = kmalloc(sizeof(device_t));
	if (!dev)
		return NULL;

	memset(dev, 0, sizeof(*dev));
	spin_init(&dev->lock);

	dev->name = name;
	dev->bus = bus;
	dev->parent = parent;
	dev->bus_data = bus_data;
	dev->driver = NULL;
	dev->driver_data = NULL;

	unsigned long flags = spin_lock_irqsave(&bus->lock);
	dev->next = bus->devices;
	bus->devices = dev;
	spin_unlock_irqrestore(&bus->lock, flags);
	log("device: created device '%s' on bus '%s'\n", name, bus->name);

	flags = spin_lock_irqsave(&bus->lock);
	for (driver_t *drv = bus->drivers; drv; drv = drv->next) {
		spin_unlock_irqrestore(&bus->lock, flags);
		if (device_try_bind(dev, drv) == 0)
			break; /* bound — stop searching */
		flags = spin_lock_irqsave(&bus->lock);
	}
	if (dev->driver == NULL)
		spin_unlock_irqrestore(&bus->lock, flags);

	return dev;
}

void device_destroy(device_t *dev)
{
	if (!dev)
		return;

	bus_type_t *bus = dev->bus;

	if (dev->driver && dev->driver->remove)
		dev->driver->remove(dev);

	unsigned long flags = spin_lock_irqsave(&bus->lock);
	device_t **pp = &bus->devices;
	while (*pp && *pp != dev)
		pp = &(*pp)->next;
	if (*pp)
		*pp = dev->next;
	spin_unlock_irqrestore(&bus->lock, flags);

	log("device: destroyed device '%s'\n", dev->name);
	kfree(dev);
}

int bus_for_each_device(bus_type_t *bus, int (*cb)(device_t *dev, void *user), void *user)
{
	if (!bus || !cb)
		return -EINVAL;

	unsigned long flags = spin_lock_irqsave(&bus->lock);
	for (device_t *dev = bus->devices; dev; dev = dev->next) {
		spin_unlock_irqrestore(&bus->lock, flags);
		int r = cb(dev, user);
		if (r)
			return r;
		flags = spin_lock_irqsave(&bus->lock);
	}
	spin_unlock_irqrestore(&bus->lock, flags);
	return 0;
}

void device_model_init(void)
{
	spin_init(&bus_list_lock);
	log("device: device model initialised\n");
}