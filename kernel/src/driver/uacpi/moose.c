#include "uacpi/status.h"
#include <stddef.h>
#include <stdint.h>
#include <uacpi/kernel_api.h>
#include <sys/moose.h>
#include <sys/klog.h>
#include <arch/cpu.h>
#include <arch/apic.h>
#include <arch/idt.h>
#include <mm/kheap.h>
#include <mm/vma.h>
#include <tsc/tsc.h>
#include <pci/pci.h>

uint64_t moose_rsdp = 0;

void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len)
{
	return vma_ioremap(current_vctx, addr, len,
			   VMA_PROT_READ | VMA_PROT_WRITE);
}

void uacpi_kernel_unmap(void *addr, uacpi_size len)
{
	vma_iounmap(current_vctx, addr, len);
}

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out_rsdp_address)
{
	if (!out_rsdp_address)
		return UACPI_STATUS_INVALID_ARGUMENT;
	*out_rsdp_address = moose_rsdp;
	return moose_rsdp ? UACPI_STATUS_OK : UACPI_STATUS_NOT_FOUND;
}

void uacpi_kernel_log(uacpi_log_level level, const uacpi_char *msg)
{
	const char *lvl = "";
	switch (level) {
	case UACPI_LOG_DEBUG:
		lvl = COL_BLUE "DEBUG" COL_RESET;
		break;
	case UACPI_LOG_TRACE:
		lvl = COL_CYAN "TRACE" COL_RESET;
		break;
	case UACPI_LOG_INFO:
		lvl = "INFO";
		break;
	case UACPI_LOG_WARN:
		lvl = COL_AMBER "WARN" COL_RESET;
		break;
	case UACPI_LOG_ERROR:
		lvl = COL_RED "ERROR" COL_RESET;
		break;
	}

	uacpi_size len = 0;
	while (msg[len])
		len++;
	char buf[384];
	uacpi_size copy = len;
	if (copy > 0 && msg[copy - 1] == '\n')
		copy--;
	if (copy >= sizeof(buf))
		copy = sizeof(buf) - 1;
	for (uacpi_size i = 0; i < copy; i++)
		buf[i] = msg[i];
	buf[copy] = '\0';

	klog("uACPI", COL_BRIGHT "%s: %s" COL_RESET, lvl, buf);
}

void *uacpi_kernel_alloc(uacpi_size size)
{
	return kmalloc(size);
}

void uacpi_kernel_free(void *mem)
{
	kfree(mem);
}

typedef struct {
	uacpi_pci_address addr;
} pci_handle_t;

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address,
					  uacpi_handle *out_handle)
{
	pci_handle_t *h = uacpi_kernel_alloc(sizeof(pci_handle_t));
	if (!h)
		return UACPI_STATUS_OUT_OF_MEMORY;
	h->addr = address;
	*out_handle = h;
	return UACPI_STATUS_OK;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle)
{
	uacpi_kernel_free(handle);
}

static pci_addr_t to_pci_addr(uacpi_pci_address a)
{
	return (pci_addr_t){
		.segment  = a.segment,
		.bus      = a.bus,
		.device   = a.device,
		.function = a.function,
	};
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle h, uacpi_size o, uacpi_u8 *v)
{
	pci_handle_t *ph = h;
	*v = pci_config_read8(to_pci_addr(ph->addr), o);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle h, uacpi_size o, uacpi_u16 *v)
{
	pci_handle_t *ph = h;
	*v = pci_config_read16(to_pci_addr(ph->addr), o);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle h, uacpi_size o, uacpi_u32 *v)
{
	pci_handle_t *ph = h;
	*v = pci_config_read32(to_pci_addr(ph->addr), o);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle h, uacpi_size o, uacpi_u8 v)
{
	pci_handle_t *ph = h;
	pci_config_write8(to_pci_addr(ph->addr), o, v);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle h, uacpi_size o, uacpi_u16 v)
{
	pci_handle_t *ph = h;
	pci_config_write16(to_pci_addr(ph->addr), o, v);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle h, uacpi_size o, uacpi_u32 v)
{
	pci_handle_t *ph = h;
	pci_config_write32(to_pci_addr(ph->addr), o, v);
	return UACPI_STATUS_OK;
}

typedef struct {
	uacpi_io_addr base;
	uacpi_size len;
} io_map_t;

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len,
				 uacpi_handle *out_handle)
{
	io_map_t *map = uacpi_kernel_alloc(sizeof(*map));
	if (!map)
		return UACPI_STATUS_OUT_OF_MEMORY;
	map->base = base;
	map->len = len;
	*out_handle = map;
	return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle)
{
	uacpi_kernel_free(handle);
}

uacpi_status uacpi_kernel_io_read8(uacpi_handle h, uacpi_size o, uacpi_u8 *v)
{
	io_map_t *map = h;
	*v = inb((uint16_t)(map->base + o));
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle h, uacpi_size o, uacpi_u16 *v)
{
	io_map_t *map = h;
	*v = inw((uint16_t)(map->base + o));
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle h, uacpi_size o, uacpi_u32 *v)
{
	io_map_t *map = h;
	*v = inl((uint16_t)(map->base + o));
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle h, uacpi_size o, uacpi_u8 v)
{
	io_map_t *map = h;
	outb((uint16_t)(map->base + o), v);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle h, uacpi_size o, uacpi_u16 v)
{
	io_map_t *map = h;
	outw((uint16_t)(map->base + o), v);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle h, uacpi_size o, uacpi_u32 v)
{
	io_map_t *map = h;
	outl((uint16_t)(map->base + o), v);
	return UACPI_STATUS_OK;
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void)
{
	return tsc_uptime_us() * 1000ULL;
}

void uacpi_kernel_stall(uacpi_u8 usec)
{
	uint64_t khz = tsc_get_khz();
	if (!khz) {
		for (volatile uint32_t i = 0; i < (uint32_t)usec * 200; i++)
			;
		return;
	}
	uint64_t target = rdtsc() + ((uint64_t)usec * khz) / 1000;
	while (rdtsc() < target)
		;
}

void uacpi_kernel_sleep(uacpi_u64 msec)
{
	uint64_t khz = tsc_get_khz();
	if (!khz) {
		for (volatile uint32_t i = 0; i < (uint32_t)msec * 200000; i++)
			;
		return;
	}
	uint64_t target = rdtsc() + msec * khz;
	while (rdtsc() < target)
		;
}

uacpi_thread_id uacpi_kernel_get_thread_id(void)
{
	static int dummy;
	return (uacpi_thread_id)(uintptr_t)&dummy;
}

uacpi_interrupt_state uacpi_kernel_disable_interrupts(void)
{
	uint64_t flags;
	__asm__ volatile("pushfq; pop %0; cli" : "=r"(flags));
	return (uacpi_interrupt_state)flags;
}

void uacpi_kernel_restore_interrupts(uacpi_interrupt_state state)
{
	if (state & 0x200)
		__asm__ volatile("sti");
}

uacpi_handle uacpi_kernel_create_mutex(void)
{
	uint8_t *m = uacpi_kernel_alloc(1);
	if (m)
		*m = 0;
	return m;
}

void uacpi_kernel_free_mutex(uacpi_handle h)
{
	uacpi_kernel_free(h);
}

uacpi_handle uacpi_kernel_create_event(void)
{
	uint8_t *e = uacpi_kernel_alloc(1);
	if (e)
		*e = 0;
	return e;
}

void uacpi_kernel_free_event(uacpi_handle h)
{
	uacpi_kernel_free(h);
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle h, uacpi_u16 t)
{
	(void)t;
	uint8_t *m = h;
	if (*m)
		return UACPI_STATUS_TIMEOUT;
	*m = 1;
	return UACPI_STATUS_OK;
}

void uacpi_kernel_release_mutex(uacpi_handle h)
{
	uint8_t *m = h;
	*m = 0;
}

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle h, uacpi_u16 t)
{
	(void)t;
	uint8_t *e = h;
	if (!*e)
		return UACPI_FALSE;
	*e = 0;
	return UACPI_TRUE;
}

void uacpi_kernel_signal_event(uacpi_handle h)
{
	uint8_t *e = h;
	*e = 1;
}

void uacpi_kernel_reset_event(uacpi_handle h)
{
	uint8_t *e = h;
	*e = 0;
}

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *r)
{
	(void)r;
	return UACPI_STATUS_UNIMPLEMENTED;
}

#define MAX_UACPI_IRQ 64

struct uacpi_irq_entry {
	uint32_t gsi;
	uint8_t vector;
	uacpi_interrupt_handler handler;
	uacpi_handle ctx;
	int used;
};

static struct uacpi_irq_entry uacpi_irqs[MAX_UACPI_IRQ];

static void uacpi_irq_dispatch(int_frame_t *frame)
{
	uint8_t vector = (uint8_t)frame->vector;
	for (size_t i = 0; i < MAX_UACPI_IRQ; i++) {
		struct uacpi_irq_entry *e = &uacpi_irqs[i];
		if (e->used && e->vector == vector) {
			e->handler(e->ctx);
			return;
		}
	}
}

uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq,
						    uacpi_interrupt_handler h,
						    uacpi_handle ctx,
						    uacpi_handle *out)
{
	if (!out)
		return UACPI_STATUS_INVALID_ARGUMENT;

	size_t slot = MAX_UACPI_IRQ;
	for (size_t i = 0; i < MAX_UACPI_IRQ; i++) {
		if (!uacpi_irqs[i].used) {
			slot = i;
			break;
		}
	}
	if (slot == MAX_UACPI_IRQ)
		return UACPI_STATUS_OUT_OF_MEMORY;

	uint8_t vector = IRQ_BASE + (uint8_t)irq;
	irq_register(vector, uacpi_irq_dispatch);

	uacpi_irqs[slot].gsi = irq;
	uacpi_irqs[slot].vector = vector;
	uacpi_irqs[slot].handler = h;
	uacpi_irqs[slot].ctx = ctx;
	uacpi_irqs[slot].used = 1;

	apic_gsi_set_mask(irq, 0);

	klog("uACPI", "installed IRQ handler: gsi=%u vector=0x%x slot=%zu",
	     irq, vector, slot);

	*out = &uacpi_irqs[slot];
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler h,
						      uacpi_handle irq_handle)
{
	(void)h;
	struct uacpi_irq_entry *e = irq_handle;
	if (!e || !e->used)
		return UACPI_STATUS_INVALID_ARGUMENT;

	apic_gsi_set_mask(e->gsi, 1);
	irq_register(e->vector, NULL);
	e->used = 0;

	klog("uACPI", "uninstalled IRQ handler: gsi=%u vector=0x%x",
	     e->gsi, e->vector);
	return UACPI_STATUS_OK;
}

uacpi_handle uacpi_kernel_create_spinlock(void)
{
	return (uacpi_handle)1;
}

void uacpi_kernel_free_spinlock(uacpi_handle h)
{
	(void)h;
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle h)
{
	(void)h;
	uint64_t flags;
	__asm__ volatile("pushfq; pop %0; cli" : "=r"(flags));
	return (uacpi_cpu_flags)flags;
}

void uacpi_kernel_unlock_spinlock(uacpi_handle h, uacpi_cpu_flags f)
{
	(void)h;
	if (f & 0x200)
		__asm__ volatile("sti");
}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type type,
					uacpi_work_handler handler,
					uacpi_handle ctx)
{
	(void)type;
	handler(ctx);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void)
{
	return UACPI_STATUS_OK;
}
