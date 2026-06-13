#include <uacpi/log.h>
#include <uacpi/kernel_api.h>
#include <lib/printk.h>
#include <lib/math.h>
#include <mm/vma.h>
#include <mm/heap.h>
#include <boot/limine.h>
#include <arch/io.h>
#include <dev/tsc.h>
#include <dev/pci.h>
#include <sys/spinlock.h>

struct io_handle {
	uacpi_io_addr base;
};

struct pci_handle {
	struct pci_addr addr;
};

struct uacpi_spinlock {
	spinlock_t lock;
};

void uacpi_early_init(void)
{
	tsc_calibrate();
}

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out_rsdp_address)
{
	if (!out_rsdp_address)
		return UACPI_STATUS_INVALID_ARGUMENT;

	if (!rsdp_request.response || !rsdp_request.response->address)
		return UACPI_STATUS_NOT_FOUND;

	*out_rsdp_address = (uacpi_phys_addr)virt_to_phys(rsdp_request.response->address);
	return UACPI_STATUS_OK;
}

void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len)
{
	if (len == 0)
		return NULL;
	return (void *)vmap_mmio(kernel_vctx, (uintptr_t)addr, (size_t)len);
}

void uacpi_kernel_unmap(void *addr, uacpi_size len)
{
	vunmap(kernel_vctx, (uintptr_t)addr, len);
}

static const char *uacpi_log_level_to_str(uacpi_log_level lvl)
{
	switch (lvl) {
	case UACPI_LOG_TRACE:
		return "trace";
	case UACPI_LOG_DEBUG:
		return "debug";
	case UACPI_LOG_WARN:
		return "warn";
	case UACPI_LOG_ERROR:
		return "error";
	case UACPI_LOG_INFO:
		return "info";
	default:
		return "unknown";
	}
}

void uacpi_kernel_log(uacpi_log_level lvl, const uacpi_char *msg)
{
	printk("uacpi: %s: %s", uacpi_log_level_to_str(lvl), msg);
}

void *uacpi_kernel_alloc(uacpi_size size)
{
	return kmalloc(size);
}

void uacpi_kernel_free(void *mem)
{
	kfree(mem);
}

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle *out_handle)
{
	struct pci_handle *h = kmalloc(sizeof(struct pci_handle));
	if (!h)
		return UACPI_STATUS_OUT_OF_MEMORY;

	h->addr.segment = address.segment;
	h->addr.bus = address.bus;
	h->addr.device = address.device;
	h->addr.function = address.function;

	*out_handle = h;
	return UACPI_STATUS_OK;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle)
{
	kfree(handle);
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle dev, uacpi_size off, uacpi_u8 *val)
{
	struct pci_handle *h = dev;
	*val = pci_config_read8(h->addr, (uint16_t)off);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle dev, uacpi_size off, uacpi_u16 *val)
{
	struct pci_handle *h = dev;
	*val = pci_config_read16(h->addr, (uint16_t)off);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle dev, uacpi_size off, uacpi_u32 *val)
{
	struct pci_handle *h = dev;
	*val = pci_config_read32(h->addr, (uint16_t)off);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle dev, uacpi_size off, uacpi_u8 val)
{
	struct pci_handle *h = dev;
	pci_config_write8(h->addr, (uint16_t)off, val);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle dev, uacpi_size off, uacpi_u16 val)
{
	struct pci_handle *h = dev;
	pci_config_write16(h->addr, (uint16_t)off, val);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle dev, uacpi_size off, uacpi_u32 val)
{
	struct pci_handle *h = dev;
	pci_config_write32(h->addr, (uint16_t)off, val);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle *out_handle)
{
	struct io_handle *h = kmalloc(sizeof(struct io_handle));
	if (!h)
		return UACPI_STATUS_OUT_OF_MEMORY;

	h->base = base;
	(void)len;
	*out_handle = h;
	return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle)
{
	kfree(handle);
}

uacpi_status uacpi_kernel_io_read8(uacpi_handle h, uacpi_size off, uacpi_u8 *v)
{
	struct io_handle *iop = h;
	*v = inb(iop->base + off);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle h, uacpi_size off, uacpi_u16 *v)
{
	struct io_handle *iop = h;
	*v = inw(iop->base + off);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle h, uacpi_size off, uacpi_u32 *v)
{
	struct io_handle *iop = h;
	*v = inl(iop->base + off);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle h, uacpi_size off, uacpi_u8 v)
{
	struct io_handle *iop = h;
	outb(iop->base + off, v);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle h, uacpi_size off, uacpi_u16 v)
{
	struct io_handle *iop = h;
	outw(iop->base + off, v);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle h, uacpi_size off, uacpi_u32 v)
{
	struct io_handle *iop = h;
	outl(iop->base + off, v);
	return UACPI_STATUS_OK;
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void)
{
	return tsc_to_ns(tsc_read());
}

void uacpi_kernel_stall(uacpi_u8 usec)
{
	uint64_t target = tsc_read() + ((uint64_t)usec * tsc_hz) / 1000000ULL;
	while (tsc_read() < target)
		;
}

void uacpi_kernel_sleep(uacpi_u64 msec)
{
	uint64_t target = tsc_read() + ((uint64_t)msec * tsc_hz) / 1000ULL;
	while (tsc_read() < target)
		;
}

uacpi_handle uacpi_kernel_create_mutex(void)
{
	uint8_t *mutex = kmalloc(sizeof(uint8_t));
	if (mutex)
		*mutex = 0;
	return mutex;
}

void uacpi_kernel_free_mutex(uacpi_handle h)
{
	kfree(h);
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle h, uacpi_u16 timeout)
{
	(void)h;
	(void)timeout;
	return UACPI_STATUS_OK;
}

void uacpi_kernel_release_mutex(uacpi_handle h)
{
	(void)h;
}

uacpi_handle uacpi_kernel_create_event(void)
{
	uint8_t *evt = kmalloc(sizeof(uint8_t));
	if (evt)
		*evt = 0;
	return evt;
}

void uacpi_kernel_free_event(uacpi_handle h)
{
	kfree(h);
}

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle h, uacpi_u16 timeout)
{
	(void)h;
	(void)timeout;
	return UACPI_TRUE;
}

void uacpi_kernel_signal_event(uacpi_handle h)
{
	(void)h;
}

void uacpi_kernel_reset_event(uacpi_handle h)
{
	(void)h;
}

uacpi_thread_id uacpi_kernel_get_thread_id(void)
{
	return 0;
}

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *req)
{
	(void)req;
	return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq,
                                                    uacpi_interrupt_handler handler,
                                                    uacpi_handle ctx,
                                                    uacpi_handle *out_irq_handle)
{
	(void)irq;
	(void)handler;
	(void)ctx;
	*out_irq_handle = NULL;
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler handler,
                                                      uacpi_handle irq_handle)
{
	(void)handler;
	(void)irq_handle;
	return UACPI_STATUS_OK;
}

uacpi_handle uacpi_kernel_create_spinlock(void)
{
	struct uacpi_spinlock *lock = kmalloc(sizeof(struct uacpi_spinlock));
	if (lock)
		spin_init(&lock->lock);
	return lock;
}

void uacpi_kernel_free_spinlock(uacpi_handle h)
{
	kfree(h);
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle h)
{
	struct uacpi_spinlock *lock = h;
	return spin_lock_irqsave(&lock->lock);
}

void uacpi_kernel_unlock_spinlock(uacpi_handle h, uacpi_cpu_flags flags)
{
	struct uacpi_spinlock *lock = h;
	spin_unlock_irqrestore(&lock->lock, flags);
}

uacpi_status
uacpi_kernel_schedule_work(uacpi_work_type type, uacpi_work_handler handler, uacpi_handle ctx)
{
	(void)type;
	(void)handler;
	(void)ctx;
	return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void)
{
	return UACPI_STATUS_UNIMPLEMENTED;
}