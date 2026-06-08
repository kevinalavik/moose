#ifndef DEV_DEV_H
#define DEV_DEV_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/klog.h>

#define DEV_COL_BRED "\x1b[1;31m"
#define DEV_COL_AMBER "\x1b[1;33m"
#define DEV_COL_RESET "\x1b[0m"

typedef enum {
	DEV_STATUS_OK = 0,
	DEV_STATUS_ERROR,
	DEV_STATUS_INVALID,
	DEV_STATUS_UNSUPPORTED,
	DEV_STATUS_NO_DEVICE
} dev_status_t;
typedef enum { DEV_TYPE_NONE = 0, DEV_TYPE_CHAR } dev_type_t;

struct handle;

typedef size_t (*dev_read_fn)(struct handle *h, void *buf, size_t len);
typedef size_t (*dev_write_fn)(struct handle *h, const void *buf, size_t len);

typedef struct dev_ops {
	dev_read_fn read;
	dev_write_fn write;
} dev_ops_t;

typedef struct handle {
	void *dev;
	const dev_ops_t *ops;
	const char *label;
	dev_type_t type;
} handle_t;

#define HANDLE_INVALID              \
	((handle_t){ .dev = NULL,   \
		     .ops = NULL,   \
		     .label = NULL, \
		     .type = DEV_TYPE_NONE })

const char *dev_status_string(dev_status_t status);
void dev_error(const char *msg);
handle_t device_handle_make(void *dev, const dev_ops_t *ops, const char *label);
bool device_handle_valid(handle_t *h);
const char *device_label(handle_t *h);
size_t device_read(handle_t *h, void *buf, size_t len);
size_t device_write(handle_t *h, const void *buf, size_t len);

#endif /* DEV_:DEV_H */