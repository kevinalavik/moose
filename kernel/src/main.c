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

#define FONT_PATH_PSF "/fonts/tty.psf"

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

__attribute__((used, section(".limine_requests_end"))) static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;

struct limine_framebuffer *moose_fb = NULL;
struct limine_memmap_response *moose_memmap = NULL;

uintptr_t moose_hhdm_off = 0;

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

static struct limine_file *find_module(const char *path)
{
    struct limine_module_response *response = module_request.response;

    if (!response)
        return NULL;

    for (uint64_t i = 0; i < response->module_count; i++)
    {
        struct limine_file *module = response->modules[i];

        if (!module || !module->path)
            continue;

        if (strcmp(module->path, path) == 0)
            return module;
    }

    return NULL;
}

void kmain(void)
{
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision))
        hcf();

    com1 = uart_init(COM1);
    if (com1.dev == NULL)
    {
        /* this is sort-of pointless rn since there will be no output if uart fails  since it inits before fb*/
        klog("early", COL_AMBER "failed to open COM1 device handle" COL_RESET);
    }

    if (!framebuffer_request.response ||
        framebuffer_request.response->framebuffer_count < 1)
    {
        klog("early", COL_BRED "failed to get framebuffer" COL_RESET);
        hcf(); /* this is a graphical OS so if no framebuffer available then fuck off*/
    }

    if (!module_request.response)
    {
        klog("early", COL_BRED "failed to get kernel modules" COL_RESET);
        hcf();
    }

    moose_fb = framebuffer_request.response->framebuffers[0];
    if (!moose_fb || !moose_fb->address)
    {
        klog("early", COL_BRED "invalid framebuffer" COL_RESET);
        hcf();
    }

    /* todo: make term support any bpp aswell as color shift */
    if (moose_fb->bpp != 32)
    {
        klog("early", COL_BRED "not a 32bpp framebuffer" COL_RESET);
        hcf();
    }

    struct limine_file *font_file = find_module(FONT_PATH_PSF);
    if (font_file)
        tty0 = console_init(moose_fb, font_file->address, (size_t)font_file->size);

    if (!tty0.dev)
        klog("early", COL_AMBER "no font module found. add tty.psf to limine.conf" COL_RESET);

    klog("early", "moose kernel v0.1.0");

    gdt_init();
    klog("early", "init GDT with kcode sel=0x%x and kdata sel=0x%x", GDT_KCODE_SEL, GDT_KDATA_SEL);

    idt_init();
    klog("early", "init IDT");

    if (!memmap_request.response)
    {
        klog("early", COL_BRED "failed to get memory map" COL_RESET);
        hcf();
    }
    moose_memmap = memmap_request.response;

    if (!hhdm_request.response)
    {
        klog("early", COL_BRED "failed to get hhdm offset" COL_RESET);
        hcf();
    }
    moose_hhdm_off = hhdm_request.response->offset;
    pmm_init();
    klog("early", "init PMM");

    /* test pmm*/ {
        uint64_t *a = PHYS_TO_VIRT(pmm_alloc());
        klog("early", "Allocate single page @ %p", a);
        pmm_ref(a); /* we now manage it */
        *a = 42;
        klog("early", "Wrote \"%d\" to %p", *a, a);
        pmm_unref(a); /* not managed by us when we are done */
        pmm_free(a);

        /*
            NOTE: the ref/unref calls are not nesecary, i just do it to test :^)
        */
    }
    klog("early", "using %s", device_label(&tty0));
    hlt();
}
