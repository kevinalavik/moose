#include <limine.h>
#define BDF_IMPLEMENTATION
#include <util/bdf.h>
#include <lib/string.h>
#include <lib/term.h>
#include <sys/moose.h>
#include <util/printf.h>
#include <sys/klog.h>
#include <arch/gdt.h>
#include <arch/cpu.h>
#include <arch/idt.h>
#include <dev/uart.h>

#define FONT_PATH "/etc/fonts/tty-big.bdf"
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
handle_t com1;

int putc(char ch)
{
    term_putc(ch);
    device_write(&com1, &ch, 1);
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
        klog("early", ANSI_YELLOW "failed to open COM1 device handle" ANSI_RESET);
    }

    if (!framebuffer_request.response ||
        framebuffer_request.response->framebuffer_count < 1)
    {
        klog("early", ANSI_BOLD_RED "failed to get framebuffer" ANSI_RESET);
        hcf(); /* this is a graphical OS so if no framebuffer available then fuck off*/
    }

    if (!module_request.response)
    {
        klog("early", ANSI_BOLD_RED "failed to get kernel modules" ANSI_RESET);
        hcf();
    }

    moose_fb = framebuffer_request.response->framebuffers[0];
    if (!moose_fb || !moose_fb->address)
    {
        klog("early", ANSI_BOLD_RED "invalid framebuffer" ANSI_RESET);
        hcf();
    }

    /* todo: make term support any bpp aswell as color shift */
    if (moose_fb->bpp != 32)
    {
        klog("early", ANSI_BOLD_RED "not a 32bpp framebuffer" ANSI_RESET);
        hcf();
    }

    struct limine_file *font_file = find_module(FONT_PATH);
    if (!font_file)
    {
        klog("early", ANSI_YELLOW "failed to get font: \"%s\". Is it in your limine.conf?" ANSI_RESET, FONT_PATH);
    }
    else
    {
        moose_font.glyphs = glyphs;
        moose_font.glyphs_capacity = FONT_MAX_GPLHYS;

        if (bdf_parse((const char *)font_file->address, (size_t)font_file->size, &moose_font) != BDF_OK)
        {
            klog("early", ANSI_YELLOW "failed to parse font: \"%s\"" ANSI_RESET, FONT_PATH);
        }
        else
            term_init(moose_fb, &moose_font);
    }

    klog("early", "moose kernel v0.1.0");

    gdt_init();
    klog("early", "init GDT with kcode sel=0x%x and kdata sel=0x%x" ANSI_RESET, GDT_KCODE_SEL, GDT_KDATA_SEL);

    idt_init();
    klog("early", "init IDT");

    kprintf(
        "\033[38;2;241;73;190m  _ _\033[38;2;214;78;210m_ ___\033[38;2;187;84;231m   __\033[38;2;160;89;251m_   _\033[38;2;136;112;255m__   \033[38;2;111;139;255m___  \033[38;2;87;166;255m ___ \033[38;2;66;192;253m  ___\033[38;2;72;209;230m   __\033[38;2;79;227;207m_   _\033[38;2;86;244;184m__   \033[38;2;108;251;159m___  \033[38;2;152;242;132m ___ \033[38;2;197;232;105m ___ \033[38;2;242;223;78m ___\033[0m\n"
        "\033[38;2;241;73;190m | '_\033[38;2;214;78;210m ` _ \033[38;2;187;84;231m\\ / _\033[38;2;160;89;251m \\ / \033[38;2;136;112;255m_ \\ /\033[38;2;111;139;255m _ \\ \033[38;2;87;166;255m/ _ \\\033[38;2;66;192;253m / _ \033[38;2;72;209;230m\\ / _\033[38;2;79;227;207m \\ / \033[38;2;86;244;184m_ \\ /\033[38;2;108;251;159m _ \\ \033[38;2;152;242;132m/ _ \\\033[38;2;197;232;105m/ __|\033[38;2;242;223;78m/ _ \\\033[0m\n"
        "\033[38;2;241;73;190m | | \033[38;2;214;78;210m| | |\033[38;2;187;84;231m | (_\033[38;2;160;89;251m) | (\033[38;2;136;112;255m_) | \033[38;2;111;139;255m(_) |\033[38;2;87;166;255m (_) \033[38;2;66;192;253m| (_)\033[38;2;72;209;230m | (_\033[38;2;79;227;207m) | (\033[38;2;86;244;184m_) | \033[38;2;108;251;159m(_) |\033[38;2;152;242;132m (_) \033[38;2;197;232;105m\\__ \\\033[38;2;242;223;78m  __/\033[0m\n"
        "\033[38;2;241;73;190m |_| \033[38;2;214;78;210m|_| |\033[38;2;187;84;231m_|\\__\033[38;2;160;89;251m_/ \\_\033[38;2;136;112;255m__/ \\\033[38;2;111;139;255m___/ \033[38;2;87;166;255m\\___/\033[38;2;66;192;253m \\___\033[38;2;72;209;230m/ \\__\033[38;2;79;227;207m_/ \\_\033[38;2;86;244;184m__/ \\\033[38;2;108;251;159m___/ \033[38;2;152;242;132m\\___/\033[38;2;197;232;105m|___/\033[38;2;242;223;78m\\___|\033[0m\n");
    kprintf("howdy!\n");

    hlt();
}