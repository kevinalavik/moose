#ifndef DEV_DEV_H
#define DEV_DEV_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/klog.h>
#include <lib/term.h>

typedef enum
{
    DEV_STATUS_OK = 0,
    DEV_STATUS_ERROR,
    DEV_STATUS_INVALID,
    DEV_STATUS_UNSUPPORTED,
    DEV_STATUS_NO_DEVICE,
} dev_status_t;

typedef enum
{
    DEV_TYPE_NONE = 0,
    DEV_TYPE_CHAR,
} dev_type_t;

struct handle;

typedef size_t (*dev_read_fn)(struct handle *handle, void *buf, size_t len);
typedef size_t (*dev_write_fn)(struct handle *handle, const void *buf, size_t len);

typedef struct dev_ops
{
    dev_read_fn read;
    dev_write_fn write;
} dev_ops_t;

typedef struct handle
{
    void *dev;
    const dev_ops_t *ops;
    const char *label;
    dev_type_t type;
} handle_t;

#define HANDLE_INVALID         \
    ((handle_t){               \
        .dev = NULL,           \
        .ops = NULL,           \
        .label = NULL,         \
        .type = DEV_TYPE_NONE, \
    })

static inline const char *dev_status_string(dev_status_t status)
{
    switch (status)
    {
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

static inline void dev_error(const char *msg)
{
    klog("dev", ANSI_BOLD_RED "dev error: %s" ANSI_RESET, msg);
}

static inline void dev_error_status(const char *msg, dev_status_t status)
{
    klog("dev",
         ANSI_BOLD_RED "dev error: %s: %s" ANSI_RESET,
         msg,
         dev_status_string(status));
}

static inline handle_t device_handle_make(
    void *dev,
    const dev_ops_t *ops,
    const char *label)
{
    if (dev == NULL)
    {
        dev_error("null device");
        return HANDLE_INVALID;
    }

    if (ops == NULL)
    {
        dev_error("null device ops");
        return HANDLE_INVALID;
    }

    return (handle_t){
        .dev = dev,
        .ops = ops,
        .label = label,
        .type = DEV_TYPE_CHAR,
    };
}

static inline bool device_handle_valid(handle_t *handle)
{
    return handle != NULL &&
           handle->dev != NULL &&
           handle->ops != NULL &&
           handle->type == DEV_TYPE_CHAR;
}

static inline const char *device_label(handle_t *handle)
{
    if (!device_handle_valid(handle))
        return NULL;

    return handle->label;
}

static inline size_t device_read(handle_t *handle, void *buf, size_t len)
{
    if (!device_handle_valid(handle))
    {
        dev_error("read on invalid handle");
        return 0;
    }

    if (buf == NULL)
    {
        dev_error("read with null buffer");
        return 0;
    }

    if (len == 0)
        return 0;

    if (handle->ops->read == NULL)
    {
        klog("dev",
             ANSI_BOLD_YELLOW "dev error: read unsupported on %s" ANSI_RESET,
             handle->label ? handle->label : "unknown");
        return 0;
    }

    return handle->ops->read(handle, buf, len);
}

static inline size_t device_write(handle_t *handle, const void *buf, size_t len)
{
    if (!device_handle_valid(handle))
    {
        dev_error("write on invalid handle");
        return 0;
    }

    if (buf == NULL)
    {
        dev_error("write with null buffer");
        return 0;
    }

    if (len == 0)
        return 0;

    if (handle->ops->write == NULL)
    {
        klog("dev",
             ANSI_BOLD_YELLOW "dev error: write unsupported on %s" ANSI_RESET,
             handle->label ? handle->label : "unknown");
        return 0;
    }

    return handle->ops->write(handle, buf, len);
}

#endif /* DEV_DEV_H */