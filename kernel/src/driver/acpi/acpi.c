#include <dev/device.h>
#include <dev/platform.h>
#include <sys/errno.h>
#include <sys/klog.h>
#include <mm/kheap.h>
#include <lib/string.h>
#include <uacpi/uacpi.h>
#include <uacpi/resources.h>

#define HID_BUF 64

static void eisa_id_to_string(uint32_t id, char *out, size_t max)
{
	static const char hex[] = "0123456789ABCDEF";
	id = __builtin_bswap32(id);
	if (max < 8)
		return;
	out[0] = 'A' + ((id >> 26) & 0x1F) - 1;
	out[1] = 'A' + ((id >> 21) & 0x1F) - 1;
	out[2] = 'A' + ((id >> 16) & 0x1F) - 1;
	uint16_t product = (uint16_t)(id & 0xFFFF);
	out[3] = hex[(product >> 12) & 0xF];
	out[4] = hex[(product >> 8) & 0xF];
	out[5] = hex[(product >> 4) & 0xF];
	out[6] = hex[product & 0xF];
	out[7] = '\0';
}

static bool extract_hid(struct uacpi_namespace_node *node, char *buf,
			size_t max)
{
	uacpi_object *ret = NULL;
	uacpi_status s = uacpi_eval_simple_typed(
		node, "_HID",
		UACPI_OBJECT_STRING_BIT | UACPI_OBJECT_BUFFER_BIT |
			UACPI_OBJECT_INTEGER_BIT,
		&ret);
	if (uacpi_unlikely_error(s) || !ret)
		return false;

	bool ok = false;
	uacpi_object_type type = uacpi_object_get_type(ret);

	if (type == UACPI_OBJECT_INTEGER) {
		uint64_t val;
		if (uacpi_object_get_integer(ret, &val) == UACPI_STATUS_OK) {
			eisa_id_to_string((uint32_t)val, buf, max);
			ok = true;
		}
	} else if (type == UACPI_OBJECT_STRING) {
		uacpi_data_view view;
		if (uacpi_object_get_string(ret, &view) == UACPI_STATUS_OK &&
		    view.text) {
			size_t len = view.length < max - 1 ? view.length :
							     max - 1;
			memcpy(buf, view.text, len);
			buf[len] = '\0';
			ok = true;
		}
	} else if (type == UACPI_OBJECT_BUFFER) {
		uacpi_data_view view;
		if (uacpi_object_get_buffer(ret, &view) == UACPI_STATUS_OK &&
		    view.bytes) {
			size_t len = view.length < max - 1 ? view.length :
							     max - 1;
			memcpy(buf, view.bytes, len);
			buf[len] = '\0';
			ok = true;
		}
	}

	uacpi_object_unref(ret);
	return ok;
}

static uacpi_iteration_decision resource_cb(void *user, uacpi_resource *res)
{
	device_t *dev = user;

	switch (res->type) {
	case UACPI_RESOURCE_TYPE_IRQ:
		if (res->irq.num_irqs > 0)
			platform_device_add_res_irq(dev, res->irq.irqs[0]);
		break;
	case UACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		if (res->extended_irq.num_irqs > 0)
			platform_device_add_res_irq(dev,
						    res->extended_irq.irqs[0]);
		break;
	case UACPI_RESOURCE_TYPE_IO:
		platform_device_add_res_io(
			dev, (uint16_t)res->io.minimum,
			(uint16_t)(res->io.minimum + res->io.length - 1));
		break;
	case UACPI_RESOURCE_TYPE_FIXED_IO:
		platform_device_add_res_io(dev, (uint16_t)res->fixed_io.address,
					   (uint16_t)(res->fixed_io.address +
						      res->fixed_io.length -
						      1));
		break;
	default:
		break;
	}

	return UACPI_ITERATION_DECISION_CONTINUE;
}

static uacpi_iteration_decision
scan_cb(void *user, struct uacpi_namespace_node *node, uacpi_u32 depth)
{
	(void)user;
	(void)depth;

	char hid[HID_BUF];
	if (!extract_hid(node, hid, sizeof(hid)))
		return UACPI_ITERATION_DECISION_CONTINUE;

	char *name = kmalloc(strlen(hid) + 1);
	if (!name)
		return UACPI_ITERATION_DECISION_CONTINUE;
	strcpy(name, hid);

	device_t *dev = kmalloc(sizeof(device_t));
	if (!dev) {
		kfree(name);
		return UACPI_ITERATION_DECISION_CONTINUE;
	}

	*dev = (device_t){
		.name = name,
		.bus = &platform_bus,
	};

	uacpi_resources *res = NULL;
	uacpi_status s = uacpi_get_current_resources(node, &res);
	if (uacpi_likely_success(s)) {
		uacpi_for_each_resource(res, resource_cb, dev);
		uacpi_free_resources(res);
	}

	device_register(dev);
	return UACPI_ITERATION_DECISION_CONTINUE;
}

int acpi_platform_scan(void)
{
	struct uacpi_namespace_node *sb =
		uacpi_namespace_get_predefined(UACPI_PREDEFINED_NAMESPACE_SB);

	uacpi_status s = uacpi_namespace_for_each_child(sb, scan_cb, NULL,
							UACPI_OBJECT_DEVICE_BIT,
							UACPI_MAX_DEPTH_ANY,
							NULL);

	if (uacpi_unlikely_error(s)) {
		klog("acpi", "namespace scan failed: %s",
		     uacpi_status_to_string(s));
		return -EIO;
	}

	return 0;
}
