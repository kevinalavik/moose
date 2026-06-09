#include <arch/acpi.h>
#include <arch/apic.h>
#include <device/device.h>
#include <device/platform.h>
#include <uacpi/status.h>
#include <limine.h>
#include <lib/string.h>
#include <tty/tty.h>
#include <clock/tsc.h>
#include <sys/moose.h>
#include <util/printf.h>
#include <sys/klog.h>
#include <arch/gdt.h>
#include <arch/cpu.h>
#include <arch/idt.h>
#include <serial/uart.h>
#include <mm/pmm.h>
#include <lib/math.h>
#include <term/builtin_font.h>
#include <fs/cpio.h>
#include <fs/vfs.h>
#include <fs/tmpfs.h>
#include <fs/devfs.h>
#include <sys/cred.h>
#include <arch/paging.h>
#include <mm/vma.h>
#include <mm/kheap.h>
#include <uacpi/sleep.h>

int system_shutdown(void)
{
	uacpi_status ret = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);
	if (uacpi_unlikely_error(ret)) {
		klog("uACPI",
		     COL_RED "failed to prepare for sleep: %s" COL_RESET,
		     uacpi_status_to_string(ret));
		return -EIO;
	}

	ret = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
	if (uacpi_unlikely_error(ret)) {
		klog("uACPI", COL_RED "failed to enter sleep: %s" COL_RESET,
		     uacpi_status_to_string(ret));
		return -EIO;
	}

	return 0;
}

__attribute__((used,
	       section(".limine_requests_start"))) static volatile uint64_t
	limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests"))) static volatile uint64_t
	limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((
	used,
	section(".limine_requests"))) static volatile struct limine_framebuffer_request
	framebuffer_request = {
		.id = LIMINE_FRAMEBUFFER_REQUEST_ID,
		.revision = 0,
	};

__attribute__((
	used,
	section(".limine_requests"))) static volatile struct limine_rsdp_request
	rsdp_request = {
		.id = LIMINE_RSDP_REQUEST_ID,
		.revision = 0,
	};

__attribute__((
	used,
	section(".limine_requests"))) static volatile struct limine_module_request
	module_request = {
		.id = LIMINE_MODULE_REQUEST_ID,
		.revision = 0,
	};

__attribute__((
	used,
	section(".limine_requests"))) static volatile struct limine_memmap_request
	memmap_request = {
		.id = LIMINE_MEMMAP_REQUEST_ID,
		.revision = 0,
	};

__attribute__((
	used,
	section(".limine_requests"))) static volatile struct limine_hhdm_request
	hhdm_request = {
		.id = LIMINE_HHDM_REQUEST_ID,
		.revision = 0,
	};

__attribute__((
	used,
	section(".limine_requests"))) static volatile struct limine_executable_address_request
	kernel_file_request = {
		.id = LIMINE_EXECUTABLE_FILE_REQUEST_ID,
		.revision = 0,
	};

__attribute__((used, section(".limine_requests_end"))) static volatile uint64_t
	limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

struct limine_framebuffer *moose_fb = NULL;
struct limine_memmap_response *moose_memmap = NULL;

uintptr_t moose_hhdm_off = 0;
uint64_t kernel_virt = 0;
uint64_t kernel_phys = 0;

static device_t uart_dev;
static device_t console_dev;

static char_dev_t *com1;
static char_dev_t *tty0;

int putc(char ch)
{
	if (com1 && char_dev_valid(com1))
		com1->write(com1, &ch, 1);
	if (tty0 && char_dev_valid(tty0))
		tty0->write(tty0, &ch, 1);
	return 1;
}

void kmain(void)
{
	cli();
	if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision))
		hcf();

	uart_dev = (device_t){ .name = "ttyS0" };
	uart_init(&uart_dev, COM1);
	com1 = char_dev_valid(&uart_dev.chardev) ? &uart_dev.chardev : NULL;
	if (!com1)
		klog("moose", "failed to open COM1 device");

	if (!framebuffer_request.response ||
	    framebuffer_request.response->framebuffer_count < 1) {
		klog("moose", COL_BRED "failed to get framebuffer" COL_RESET);
		hcf();
	}

	if (!module_request.response)
		klog("moose", COL_AMBER "no kernel modules present" COL_RESET);

	moose_fb = framebuffer_request.response->framebuffers[0];
	if (!moose_fb || !moose_fb->address) {
		klog("moose", COL_BRED "invalid framebuffer" COL_RESET);
		hcf();
	}

	if (moose_fb->bpp < 8 || moose_fb->bpp % 8 != 0) {
		klog("moose",
		     COL_BRED "unsupported framebuffer bpp: %d" COL_RESET,
		     moose_fb->bpp);
		hcf();
	}

	console_dev = (device_t){ .name = "tty0" };
	console_init(moose_fb, &ttyfont, FONT_SIZE, &console_dev);
	tty0 = char_dev_valid(&console_dev.chardev) ? &console_dev.chardev :
						      NULL;
	tsc_init();

	cred_init();

	gdt_init();
	idt_init();

	if (!memmap_request.response) {
		klog("moose", COL_BRED "failed to get memory map" COL_RESET);
		hcf();
	}
	moose_memmap = memmap_request.response;

	if (!hhdm_request.response) {
		klog("moose", COL_BRED "failed to get hhdm offset" COL_RESET);
		hcf();
	}
	moose_hhdm_off = hhdm_request.response->offset;

	if (rsdp_request.response) {
		uint64_t rsdp_hhdm = (uint64_t)rsdp_request.response->address;
		moose_rsdp = rsdp_hhdm - moose_hhdm_off;
	}

	pmm_init();

	{
		uint64_t *a = PHYS_TO_VIRT(pmm_alloc());
		klog("moose", "allocate single page @ %p", a);
		pmm_ref(a);
		*a = 42;
		klog("moose", "wrote \"%d\" to %p", *a, a);
		pmm_unref(a);
		pmm_free(a);
	}

	struct limine_file *initrd = NULL;
	if (module_request.response) {
		klog("moose", "module list:");
		for (uint64_t i = 0; i < module_request.response->module_count;
		     i++) {
			struct limine_file *mod =
				module_request.response->modules[i];
			klog("moose", "  [%d] %s: %d bytes", i, mod->path,
			     mod->size);
			if (strcmp(mod->path, "/boot/initrd.cpio") == 0)
				initrd = mod;
		}
	}

	if (!kernel_file_request.response) {
		klog("moose", COL_BRED "failed to get kernel info" COL_RESET);
		hcf();
	}
	kernel_virt = kernel_file_request.response->virtual_base;
	kernel_phys = kernel_file_request.response->physical_base;
	paging_init();

	static vctx_t kernel_vctx;
	vma_init(&kernel_vctx, PHYS_TO_VIRT(kernel_ptable));
	current_vctx = &kernel_vctx;

	superblock_t *sb = tmpfs_mount();
	if (!sb) {
		klog("moose", COL_BRED "failed to mount root tmpfs" COL_RESET);
		hcf();
	}
	vfs_mount("/", sb);

	if (initrd) {
		klog("moose", "found initrd @ %s", initrd->path);
		cpio_archive_extract(sb->s_root, initrd->address, initrd->size);
	}

	vfs_mkdir_p(sb->s_root, "dev", S_IFDIR | 0755);
	devfs_init();

	bus_register(&platform_bus);

	platform_device_add_res_io(&uart_dev, 0x3F8, 0x3FF);
	platform_device_add_res_irq(&uart_dev, 4);
	uart_dev.bus = &platform_bus;

	console_dev.bus = &platform_bus;
	device_register(&uart_dev);
	device_register(&console_dev);

	acpi_init();
	apic_init();
	driver_init_all();
	bus_rescan_devices();

	kprintf("moose kernel v0.1.0\n");

	file_t *out = vfs_open("/dev/tty0", O_WRONLY);
	if (!out) {
		klog("moose", COL_BRED "failed to open /dev/tty0" COL_RESET);
	} else {
		const char *msg = "Hello via VFS!\n";
		vfs_write(out, msg, strlen(msg));
	}

	hlt();
}
