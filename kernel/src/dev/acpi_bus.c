#include <dev/acpi_bus.h>
#include <dev/device.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/printk.h>
#include <sys/errno.h>

#include <uacpi/uacpi.h>
#include <uacpi/namespace.h>
#include <uacpi/utilities.h>
#include <uacpi/resources.h>

static bool acpi_bus_match(device_t *dev, driver_t *drv)
{
	if (!dev->bus_data || !drv->id_table)
		return false;

	const acpi_device_info_t *ai = (const acpi_device_info_t *)dev->bus_data;
	const uacpi_namespace_node_info *info = ai->info;

	if (!info)
		return false;

	for (const device_id_t *id = drv->id_table; id->id; id++) {
		if ((info->flags & UACPI_NS_NODE_INFO_HAS_HID) &&
		    strcmp(id->id, info->hid.value) == 0)
			return true;

		if (info->flags & UACPI_NS_NODE_INFO_HAS_CID) {
			for (uacpi_u32 i = 0; i < info->cid.num_ids; i++) {
				if (strcmp(id->id, info->cid.ids[i].value) == 0)
					return true;
			}
		}
	}

	return false;
}

static int acpi_bus_probe(device_t *dev, driver_t *drv)
{
	if (!drv->probe)
		return 0;

	if (!dev->bus_data || !drv->id_table)
		return -ENODEV;

	const acpi_device_info_t *ai = (const acpi_device_info_t *)dev->bus_data;
	const uacpi_namespace_node_info *info = ai->info;

	if (!info)
		return -ENODEV;

	const device_id_t *matched = NULL;
	for (const device_id_t *id = drv->id_table; id->id; id++) {
		if ((info->flags & UACPI_NS_NODE_INFO_HAS_HID) &&
		    strcmp(id->id, info->hid.value) == 0) {
			matched = id;
			break;
		}
		if (info->flags & UACPI_NS_NODE_INFO_HAS_CID) {
			for (uacpi_u32 i = 0; i < info->cid.num_ids; i++) {
				if (strcmp(id->id, info->cid.ids[i].value) == 0) {
					matched = id;
					break;
				}
			}
			if (matched)
				break;
		}
	}

	if (!matched)
		return -ENODEV;

	return drv->probe(dev, matched);
}

bus_type_t acpi_bus = {
    .name = "acpi",
    .match = acpi_bus_match,
    .probe = acpi_bus_probe,
    .drivers = NULL,
    .devices = NULL,
    .next = NULL,
};

static uacpi_iteration_decision
acpi_bus_init_one_device(void *ctx, uacpi_namespace_node *node, uacpi_u32 node_depth)
{
	(void)ctx;
	(void)node_depth;

	uacpi_namespace_node_info *info = NULL;
	uacpi_status st = uacpi_get_namespace_node_info(node, &info);
	if (uacpi_unlikely_error(st)) {
		const char *path = uacpi_namespace_node_generate_absolute_path(node);
		log("acpi_bus: failed to get info for %s: %s\n",
		    path ? path : "<?>",
		    uacpi_status_to_string(st));
		if (path)
			uacpi_free_absolute_path(path);
		return UACPI_ITERATION_DECISION_CONTINUE;
	}

	if (!(info->flags & UACPI_NS_NODE_INFO_HAS_HID) &&
	    !(info->flags & UACPI_NS_NODE_INFO_HAS_CID)) {
		uacpi_free_namespace_node_info(info);
		return UACPI_ITERATION_DECISION_CONTINUE;
	}

	const char *dev_name;
	if (info->flags & UACPI_NS_NODE_INFO_HAS_HID)
		dev_name = info->hid.value;
	else
		dev_name = "ACPI-device";

	acpi_device_info_t *ai = kmalloc(sizeof(acpi_device_info_t));
	if (!ai) {
		uacpi_free_namespace_node_info(info);
		return UACPI_ITERATION_DECISION_CONTINUE;
	}
	ai->node = node;
	ai->info = info;

	device_t *dev = device_create(&acpi_bus, NULL, dev_name, ai);
	if (!dev) {
		uacpi_free_namespace_node_info(info);
		kfree(ai);
		return UACPI_ITERATION_DECISION_CONTINUE;
	}

	if (dev->driver)
		log("acpi_bus: '%s' bound to driver '%s'\n", dev_name, dev->driver->name);
	else
		log("acpi_bus: '%s' has no driver\n", dev_name);

	return UACPI_ITERATION_DECISION_CONTINUE;
}

void acpi_bus_init(void)
{
	bus_register(&acpi_bus);
	log("acpi_bus: initialised\n");
}

void acpi_bus_enumerate(void)
{
	log("acpi_bus: enumerating ACPI namespace...\n");

	uacpi_status st = uacpi_namespace_for_each_child(uacpi_namespace_root(),
	                                                 acpi_bus_init_one_device,
	                                                 NULL,
	                                                 UACPI_OBJECT_DEVICE_BIT,
	                                                 UACPI_MAX_DEPTH_ANY,
	                                                 NULL);

	if (uacpi_unlikely_error(st))
		log("acpi_bus: enumeration error: %s\n", uacpi_status_to_string(st));
	else
		log("acpi_bus: enumeration complete\n");
}