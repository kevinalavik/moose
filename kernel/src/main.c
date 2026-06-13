#include <boot/limine.h>
#include <dev/debugcon.h>
#include <arch/cpu.h>
#include <arch/cpuid.h>
#include <lib/printk.h>
#include <lib/kconsole.h>
#include <arch/gdt.h>
#include <arch/idt.h>
#include <sys/panic.h>
#include <mm/ealloc.h>
#include <mm/pfn.h>
#include <mm/palloc.h>
#include <arch/paging.h>
#include <mm/heap.h>
#include <mm/vma.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include <uacpi/event.h>
#include <uacpi/resources.h>
#include <uacpi/namespace.h>
#include <dev/tsc.h>
#include <dev/pci.h>
#include <sys/apic.h>
#include <dev/pit.h>

uint64_t kernel_phys = 0;
uint64_t kernel_virt = 0;

void putc(char ch)
{
	char s[2] = {ch, '\0'};
	dbg_write(s);
	kconsole_write(s);
}

void kernel_entry(void)
{
	cli(); /* disable interrupts*/

	if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
		printk("error: limine base revision unsuported\n");
		hcf();
	}
	if (!fb_request.response->framebuffers[0]) {
		printk("warn: invalid framebuffer response?\n");
	} else {
		kconsole_init(fb_request.response->framebuffers[0]);
	}

	tsc_calibrate();

	printk("boot: moose-kernel v%d.%d.%d%s\n", VER_MAJOR, VER_MINOR, VER_PATCH, VER_NOTE);
	printk("boot: using framebuffer0 for kconsole (%dx%d)\n",
	       fb_request.response->framebuffers[0]->width,
	       fb_request.response->framebuffers[0]->height);
	printk("boot: running on a %s\n", get_cpu_string());

	/* setup gdt and interrupts */
	gdt_init();
	idt_init();
	// *(uint64_t *)0xdeadbeef = 42;

	/* memory */
	if (!memmap_request.response) {
		panic(NULL, "failed to get memmap");
	}

	if (!hhdm_request.response) {
		panic(NULL, "failed to get HHDM info");
	}

	for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
		struct limine_memmap_entry *m = memmap_request.response->entries[i];
		printk("mm: region: 0x%.16llx->0x%.16llx (usable=%s)\n",
		       m->base,
		       m->base + m->length,
		       m->type == LIMINE_MEMMAP_USABLE ? "yes" : "no");
	}

	ealloc_init(memmap_request.response);
	pfn_init(memmap_request.response);
	palloc_init();

	/* load paging */
	if (!kaddr_request.response) {
		panic(NULL, "failed to get kernel address");
	}
	kernel_phys = kaddr_request.response->physical_base;
	kernel_virt = kaddr_request.response->virtual_base;
	paging_init();

	/* heap */
	heap_init();

	/* heap test*/ {
		uint64_t *a = kmalloc(sizeof(uint64_t));
		if (!a)
			panic(NULL, "failed to alloc %u bytes on heap", sizeof(uint64_t));
		printk("test: allocated %u bytes -> %p\n", sizeof(uint64_t), a);
		*a = 42;
		printk("test: wrote %d to %p\n", *a, a);
		kfree(a);
	}

	/* setup kernel VMA ctx and test anon write*/
	vctx_t kvctx;
	vinit(&kvctx, (ptable_t *)phys_to_virt((uint64_t)kernel_ptable));
	kernel_vctx = &kvctx;

	uint64_t *addr = (uint64_t *)vmap_anon(&kvctx, PAGE_SIZE);
	printk("test: vmap returned %p\n", (void *)addr);
	*addr = 0xC0FFEE;
	printk("test: wrote 0x%llx to %p, read back 0x%llx\n", (uint64_t)0xC0FFEE, addr, *addr);

	/* cleanup */
	while (kvctx.vma_list)
		vdestroy(&kvctx, kvctx.vma_list);

	/* init uACPI */
	{
		uacpi_status ret = uacpi_initialize(0);
		if (uacpi_unlikely_error(ret)) {
			panic(NULL, "uacpi_initialize error: %s", uacpi_status_to_string(ret));
		}

		ret = uacpi_namespace_load();
		if (uacpi_unlikely_error(ret)) {
			panic(NULL, "uacpi_namespace_load error: %s", uacpi_status_to_string(ret));
		}

		ret = uacpi_namespace_initialize();
		if (uacpi_unlikely_error(ret)) {
			panic(NULL,
			      "uacpi_namespace_initialize error: %s",
			      uacpi_status_to_string(ret));
		}

		ret = uacpi_finalize_gpe_initialization();
		if (uacpi_unlikely_error(ret)) {
			panic(NULL,
			      "uACPI GPE initialization error: %s",
			      uacpi_status_to_string(ret));
		}
	}

	/* list devices */
	pci_scan();

	/* setup apic */
	apic_init();

	/* setup pit timer */
	pit_init();


	printk("----------------------------------------------------------------\n");
	printk("moose-kernel v%d.%d.%d%s finished loading, thanks for your patience\n",
	       VER_MAJOR,
	       VER_MINOR,
	       VER_PATCH,
	       VER_NOTE);

	/* enable interrupts and halt */
	sti();
	hcf();
}