#include <dev/dev.h>

const char *dev_status_string(dev_status_t status)
{
	switch (status) {
	case DEV_STATUS_OK:
		return "ok";
	case DEV_STATUS_ERROR:
		return "error";
	case DEV_STATUS_INVALID:
		return "invalid";
	case DEV_STATUS_UNSUPPORTED:
		return "unsupported";
	case DEV_STATUS_NO_DEVICE:
		return "no device";
	default:
		return "unknown";
	}
}

void dev_error(const char *msg)
{
	klog("dev", DEV_COL_BRED "dev error: %s" DEV_COL_RESET, msg);
}

handle_t device_handle_make(void *dev, const dev_ops_t *ops, const char *label)
{
	if (!dev) {
		dev_error("null device");
		return HANDLE_INVALID;
	}
	if (!ops) {
		dev_error("null device ops");
		return HANDLE_INVALID;
	}
	return (handle_t){
		.dev = dev, .ops = ops, .label = label, .type = DEV_TYPE_CHAR
	};
}

bool device_handle_valid(handle_t *h)
{
	return h && h->dev && h->ops && h->type == DEV_TYPE_CHAR;
}

const char *device_label(handle_t *h)
{
	return device_handle_valid(h) ? h->label : NULL;
}

size_t device_read(handle_t *h, void *buf, size_t len)
{
	if (!device_handle_valid(h)) {
		dev_error("read on invalid handle");
		return 0;
	}
	if (!buf) {
		dev_error("read with null buffer");
		return 0;
	}
	if (!len)
		return 0;
	if (!h->ops->read) {
		klog("dev",
		     DEV_COL_AMBER "read unsupported on %s" DEV_COL_RESET,
		     h->label ? h->label : "unknown");
		return 0;
	}
	return h->ops->read(h, buf, len);
}

size_t device_write(handle_t *h, const void *buf, size_t len)
{
	if (!device_handle_valid(h)) {
		dev_error("write on invalid handle");
		return 0;
	}
	if (!buf) {
		dev_error("write with null buffer");
		return 0;
	}
	if (!len)
		return 0;
	if (!h->ops->write) {
		klog("dev",
		     DEV_COL_AMBER "write unsupported on %s" DEV_COL_RESET,
		     h->label ? h->label : "unknown");
		return 0;
	}
	return h->ops->write(h, buf, len);
}
