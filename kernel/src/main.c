#include <limine.h>
#include <lib/string.h>
#include <dev/tty.h>
#include <sys/moose.h>
#include <util/printf.h>
#include <sys/klog.h>
#include <arch/gdt.h>
#include <arch/cpu.h>
#include <arch/idt.h>
#include <dev/uart.h>
#include <mm/pmm.h>
#include <lib/math.h>
#include <term/builtin_font.h>
#include <fs/cpio.h>
#include <arch/paging.h>

__attribute__((used, section(".limine_requests_start"))) static volatile uint64_t limine_requests_start_marker[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests"))) static volatile uint64_t limine_base_revision[] =
    LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests"))) static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_executable_address_request kernel_file_request = {
    .id = LIMINE_EXECUTABLE_FILE_REQUEST_ID,
    .revision = 0};

__attribute__((used, section(".limine_requests_end"))) static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;

struct limine_framebuffer *moose_fb = NULL;
struct limine_memmap_response *moose_memmap = NULL;

uintptr_t moose_hhdm_off = 0;
uint64_t kernel_virt = 0;
uint64_t kernel_phys = 0;

handle_t com1;
handle_t tty0;
int putc(char ch)
{
    if (com1.dev)
        device_write(&com1, &ch, 1);
    if (tty0.dev)
        device_write(&tty0, &ch, 1);
    return 1;
}

void kmain(void)
{
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision))
        hcf();

    com1 = uart_init(COM1);
    if (com1.dev == NULL)
    {
        /* this is sort-of pointless rn since there will be no output if uart fails  since it inits before fb*/
        klog("moose", COL_AMBER "failed to open COM1 device handle" COL_RESET);
    }

    if (!framebuffer_request.response ||
        framebuffer_request.response->framebuffer_count < 1)
    {
        klog("moose", COL_BRED "failed to get framebuffer" COL_RESET);
        hcf(); /* this is a graphical OS so if no framebuffer available then fuck off*/
    }

    if (!module_request.response)
    {
        klog("moose", COL_AMBER "failed to get kernel modules, no present?" COL_RESET);
    }

    moose_fb = framebuffer_request.response->framebuffers[0];
    if (!moose_fb || !moose_fb->address)
    {
        klog("moose", COL_BRED "invalid framebuffer" COL_RESET);
        hcf();
    }

    if (moose_fb->bpp < 8 || moose_fb->bpp % 8 != 0)
    {
        klog("moose", COL_BRED "unsupported framebuffer bpp: %d" COL_RESET, moose_fb->bpp);
        hcf();
    }

    tty0 = console_init(moose_fb, &ttyfont, FONT_SIZE);
    klog("moose", "using %s", device_label(&tty0));

    gdt_init();
    idt_init();

    if (!memmap_request.response)
    {
        klog("moose", COL_BRED "failed to get memory map" COL_RESET);
        hcf();
    }
    moose_memmap = memmap_request.response;

    if (!hhdm_request.response)
    {
        klog("moose", COL_BRED "failed to get hhdm offset" COL_RESET);
        hcf();
    }
    moose_hhdm_off = hhdm_request.response->offset;
    pmm_init();

    /* test pmm*/ {
        uint64_t *a = PHYS_TO_VIRT(pmm_alloc());
        klog("moose", "Allocate single page @ %p", a);
        pmm_ref(a); /* we now manage it */
        *a = 42;
        klog("moose", "Wrote \"%d\" to %p", *a, a);
        pmm_unref(a); /* not managed by us when we are done */
        pmm_free(a);
        /*
            NOTE: the ref/unref calls are not nesecary, i just do it to test :^)
        */
    }

    /* list limine modules and get the initrd */
    if (module_request.response)
    {
        struct limine_file *initrd = NULL;
        klog("moose", "module list:");
        for (uint64_t i = 0; i < module_request.response->module_count; i++)
        {
            struct limine_file *mod = module_request.response->modules[i];
            klog("moose", "  [%d] %s: %d bytes", i, mod->path, mod->size);
            if (strcmp(mod->path, "/boot/initrd.cpio") == 0)
                initrd = mod;
        }

        if (initrd)
        {
            klog("moose", "found initrd @ %s", initrd->path);
            cpio_parse(initrd->address, initrd->size);
        }
    }

    if (!kernel_file_request.response)
    {
        klog("moose", COL_BRED "failed to get kernel info" COL_RESET);
        hcf();
    }

    kernel_virt = kernel_file_request.response->virtual_base;
    kernel_phys = kernel_file_request.response->physical_base;
    paging_init();
    hlt();
}
