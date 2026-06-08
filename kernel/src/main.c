#include <limine.h>
#include <lib/string.h>
#include <dev/tty.h>
#include <dev/tsc.h>
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
#include <fs/vfs.h>
#include <fs/tmpfs.h>
#include <fs/devfs.h>
#include <sys/cred.h>
#include <arch/paging.h>
#include <mm/vma.h>
#include <mm/kheap.h>

__attribute__((used, section(".limine_requests_start"))) static volatile uint64_t limine_requests_start_marker[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests"))) static volatile uint64_t limine_base_revision[] =
    LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests"))) static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_executable_address_request kernel_file_request = {
    .id = LIMINE_EXECUTABLE_FILE_REQUEST_ID,
    .revision = 0,
};

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

static void mode_str(mode_t mode, char out[11])
{
    out[0] = S_ISDIR(mode) ? 'd' : S_ISCHR(mode) ? 'c'
                               : S_ISBLK(mode)   ? 'b'
                               : S_ISLNK(mode)   ? 'l'
                                                 : '-';
    out[1] = (mode & S_IRUSR) ? 'r' : '-';
    out[2] = (mode & S_IWUSR) ? 'w' : '-';
    out[3] = (mode & S_IXUSR) ? 'x' : '-';
    out[4] = (mode & S_IRGRP) ? 'r' : '-';
    out[5] = (mode & S_IWGRP) ? 'w' : '-';
    out[6] = (mode & S_IXGRP) ? 'x' : '-';
    out[7] = (mode & S_IROTH) ? 'r' : '-';
    out[8] = (mode & S_IWOTH) ? 'w' : '-';
    out[9] = (mode & S_IXOTH) ? 'x' : '-';
    out[10] = '\0';
}

static void fmt_size(uint64_t size, char out[8])
{
    const char *units = "BKMGT";
    int u = 0;
    uint64_t v = size;

    while (v >= 1024 && u < 4)
    {
        v /= 1024;
        u++;
    }

    if (u == 0)
        ksnprintf(out, 8, "%lluB", (unsigned long long)v);
    else
        ksnprintf(out, 8, "%llu%c", (unsigned long long)v, units[u]);
}

#define LS_MAX_SUBDIRS 64

static void ls_dir(const char *path, inode_t *dir, int depth)
{
    char perms[11];
    char szstr[8];
    dirent_t de;
    stat_t st;
    char indent[32];
    int ind = depth * 2;

    inode_t *subdirs[LS_MAX_SUBDIRS];
    char subpaths[LS_MAX_SUBDIRS][VFS_PATH_MAX];
    int nsubs = 0;

    if (ind >= (int)sizeof(indent) - 1)
        ind = (int)sizeof(indent) - 2;
    for (int i = 0; i < ind; i++)
        indent[i] = ' ';
    indent[ind] = '\0';

    file_t *f = kmalloc(sizeof(file_t));
    if (!f)
        return;
    f->f_inode = dir;
    f->f_op = dir->i_fop;
    f->f_pos = 0;
    f->f_flags = O_RDONLY;

    while (f->f_op && f->f_op->readdir &&
           f->f_op->readdir(f, &de, &f->f_pos) == 0)
    {
        if (strcmp(de.d_name, ".") == 0 || strcmp(de.d_name, "..") == 0)
            continue;

        char childpath[VFS_PATH_MAX];
        ksnprintf(childpath, sizeof(childpath), "%s%s", path, de.d_name);

        inode_t *child = vfs_inode(childpath);
        if (!child)
            child = vfs_lookup(dir, de.d_name);
        if (!child)
            continue;

        if (child->i_ops && child->i_ops->getattr)
            child->i_ops->getattr(child, &st);
        else
        {
            st.st_ino = child->i_ino;
            st.st_mode = child->i_mode;
            st.st_nlink = child->i_nlink;
            st.st_size = child->i_size;
            st.st_rdev = child->i_rdev;
        }

        mode_str(st.st_mode, perms);
        fmt_size(st.st_size, szstr);

        if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode))
        {
            kprintf("%s%s %2llu %u %u %3u,%3u %s%s\n",
                    indent, perms,
                    (unsigned long long)st.st_nlink,
                    st.st_uid, st.st_gid,
                    MAJOR(st.st_rdev), MINOR(st.st_rdev),
                    path, de.d_name);
        }
        else
        {
            kprintf("%s%s %2llu %u %u %6s %s%s\n",
                    indent, perms,
                    (unsigned long long)st.st_nlink,
                    st.st_uid, st.st_gid,
                    szstr,
                    path, de.d_name);
        }

        if (S_ISDIR(st.st_mode) && depth < 4 && nsubs < LS_MAX_SUBDIRS)
        {
            subdirs[nsubs] = child;
            ksnprintf(subpaths[nsubs], VFS_PATH_MAX, "%s/", childpath);
            nsubs++;
        }
    }

    kfree(f);

    for (int i = 0; i < nsubs; i++)
    {
        kprintf("%s\n", subpaths[i]);
        ls_dir(subpaths[i], subdirs[i], depth + 1);
    }
}

void kmain(void)
{
    struct limine_file *initrd = NULL;

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision))
        hcf();

    com1 = uart_init(COM1);
    if (com1.dev == NULL)
        klog("moose", COL_AMBER "failed to open COM1 device handle" COL_RESET);

    if (!framebuffer_request.response ||
        framebuffer_request.response->framebuffer_count < 1)
    {
        klog("moose", COL_BRED "failed to get framebuffer" COL_RESET);
        hcf();
    }

    if (!module_request.response)
        klog("moose", COL_AMBER "no kernel modules present" COL_RESET);

    moose_fb = framebuffer_request.response->framebuffers[0];
    if (!moose_fb || !moose_fb->address)
    {
        klog("moose", COL_BRED "invalid framebuffer" COL_RESET);
        hcf();
    }

    if (moose_fb->bpp < 8 || moose_fb->bpp % 8 != 0)
    {
        klog("moose", COL_BRED "unsupported framebuffer bpp: %d" COL_RESET,
             moose_fb->bpp);
        hcf();
    }

    tty0 = console_init(moose_fb, &ttyfont, FONT_SIZE);
    klog("moose", "using %s", device_label(&tty0));
    tsc_init();

    cred_init();

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

    {
        uint64_t *a = PHYS_TO_VIRT(pmm_alloc());
        klog("moose", "allocate single page @ %p", a);
        pmm_ref(a);
        *a = 42;
        klog("moose", "wrote \"%d\" to %p", *a, a);
        pmm_unref(a);
        pmm_free(a);
    }

    if (module_request.response)
    {
        klog("moose", "module list:");
        for (uint64_t i = 0; i < module_request.response->module_count; i++)
        {
            struct limine_file *mod = module_request.response->modules[i];
            klog("moose", "  [%d] %s: %d bytes", i, mod->path, mod->size);
            if (strcmp(mod->path, "/boot/initrd.cpio") == 0)
                initrd = mod;
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

    static vctx_t kernel_vctx;
    vma_init(&kernel_vctx, PHYS_TO_VIRT(kernel_ptable));
    current_vctx = &kernel_vctx;

    superblock_t *sb = tmpfs_mount();
    if (!sb)
    {
        klog("moose", COL_BRED "failed to mount root tmpfs" COL_RESET);
        hcf();
    }
    vfs_mount("/", sb);

    if (initrd)
    {
        klog("moose", "found initrd @ %s", initrd->path);
        cpio_archive_extract(sb->s_root, initrd->address, initrd->size);
    }

    vfs_mkdir_p(sb->s_root, "dev", S_IFDIR | 0755);
    devfs_init();

    if (device_handle_valid(&com1))
        devfs_register("com1", &com1);
    if (device_handle_valid(&tty0))
        devfs_register("tty0", &tty0);

    kprintf("moose kernel v0.1.0\n");
    kprintf("/:\n");
    ls_dir("/", sb->s_root, 0);

    file_t *out = vfs_open("/dev/tty0", O_WRONLY);
    if (!out)
    {
        klog("moose", COL_BRED "failed to open /dev/tty0" COL_RESET);
    }
    else
    {
        const char *msg = "Hello via VFS!\n";
        vfs_write(out, msg, strlen(msg));
    }
    hlt();
}