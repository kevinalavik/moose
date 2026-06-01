#include <limine.h>
#define BDF_IMPLEMENTATION
#include <util/bdf.h>
#include <lib/string.h>
#include <lib/term.h>
#include <sys/moose.h>
#include <util/printf.h>

#define FONT_PATH "/etc/fonts/tty.bdf"
#define FONT_MAX_GPLHYS 2048

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

__attribute__((used, section(".limine_requests_end"))) static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;

struct limine_framebuffer *moose_fb;
BDF_Font moose_font;

static BDF_Glyph glyphs[FONT_MAX_GPLHYS];

static void hcf(void)
{
    for (;;)
        asm volatile("hlt");
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

    if (!framebuffer_request.response ||
        framebuffer_request.response->framebuffer_count < 1)
        hcf();

    if (!module_request.response)
        hcf();

    moose_fb = framebuffer_request.response->framebuffers[0];

    if (!moose_fb || !moose_fb->address)
        hcf();

    if (moose_fb->bpp != 32)
        hcf();

    struct limine_file *font_file = find_module(FONT_PATH);

    if (!font_file)
        hcf();

    moose_font.glyphs = glyphs;
    moose_font.glyphs_capacity = FONT_MAX_GPLHYS;

    if (bdf_parse(
            (const char *)font_file->address,
            (size_t)font_file->size,
            &moose_font) != BDF_OK)
        hcf();

    term_init(moose_fb, &moose_font);
    kprintf("Hello from %s kernel\n", "moose");
    kprintf("%s %c %d %u 0x%x %p\n", "test", 'A', -123, 456, 0xdeadbeef, &moose_font);
    kprintf("Loaded font:\n");
    kprintf("  Family: %s\n", moose_font.props.family_name);
    kprintf("  Copyright: %s\n", moose_font.props.copyright);
    kprintf("  Notice: %s\n", moose_font.props.notice);

    hcf();
}