#include <arch/paging.h>
#include <sys/moose.h>
#include <mm/pmm.h>
#include <sys/klog.h>
#include <term/ansi.h>
#include <lib/string.h>
#include <arch/cpu.h>
#include <lib/math.h>
#include <sys/klog.h>

ptable_t *kernel_ptable = NULL;
extern char __limine_requests_start[];
extern char __limine_requests_end[];
extern char __text_start[];
extern char __text_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char __data_end[];

#define PT_MASK 0x1FFULL
#define PML4_INDEX(va) (((va) >> 39) & PT_MASK)
#define PDPT_INDEX(va) (((va) >> 30) & PT_MASK)
#define PD_INDEX(va) (((va) >> 21) & PT_MASK)
#define PT_INDEX(va) (((va) >> 12) & PT_MASK)
#define PAGE_OFFSET(va) ((va) & 0xFFFULL)

uint64_t virt_to_phys(ptable_t *pml4, uint64_t vaddr)
{
    ptable_t *pdpt;
    ptable_t *pd;
    ptable_t *pt;
    uint64_t entry;

    /* page map level 4 entry */
    entry = pml4->entries[PML4_INDEX(vaddr)];
    if (!(entry & PTE_PRESENT))
        return 0;

    pdpt = (ptable_t *)PHYS_TO_VIRT(entry & PTE_ADDR_MASK);

    /* page directory pointer table entry (long ahh intel name) */
    entry = pdpt->entries[PDPT_INDEX(vaddr)];
    if (!(entry & PTE_PRESENT))
        return 0;

    pd = (ptable_t *)PHYS_TO_VIRT(entry & PTE_ADDR_MASK);

    /* page directory entry */
    entry = pd->entries[PD_INDEX(vaddr)];
    if (!(entry & PTE_PRESENT))
        return 0;

    pt = (ptable_t *)PHYS_TO_VIRT(entry & PTE_ADDR_MASK);

    /* page table entry */
    entry = pt->entries[PT_INDEX(vaddr)];
    if (!(entry & PTE_PRESENT))
        return 0;

    uint64_t paddr = entry & PTE_ADDR_MASK;
    return paddr + PAGE_OFFSET(vaddr);
}

int map_page(ptable_t *pml4, uint64_t vaddr, uint64_t paddr, uint64_t flags)
{
    ptable_t *pdpt;
    ptable_t *pd;
    ptable_t *pt;
    uint64_t entry;
    uint64_t phys;

    entry = pml4->entries[PML4_INDEX(vaddr)];
    if (!(entry & PTE_PRESENT))
    {
        phys = (uint64_t)pmm_alloc();
        if (!phys)
        {
            klog("paging", COL_AMBER "failed to allocate pdpt" COL_RESET);
            return -1;
        }

        pdpt = (ptable_t *)PHYS_TO_VIRT(phys);
        memset(pdpt, 0, PAGE_SIZE);
        pml4->entries[PML4_INDEX(vaddr)] = phys | PTE_PRESENT | PTE_RW;
        entry = pml4->entries[PML4_INDEX(vaddr)];
    }

    pdpt = (ptable_t *)PHYS_TO_VIRT(entry & PTE_ADDR_MASK);
    entry = pdpt->entries[PDPT_INDEX(vaddr)];
    if (!(entry & PTE_PRESENT))
    {
        phys = (uint64_t)pmm_alloc();
        if (!phys)
        {
            klog("paging", COL_AMBER "failed to allocate pd" COL_RESET);
            return -1;
        }

        pd = (ptable_t *)PHYS_TO_VIRT(phys);
        memset(pd, 0, PAGE_SIZE);
        pdpt->entries[PDPT_INDEX(vaddr)] = phys | PTE_PRESENT | PTE_RW;
        entry = pdpt->entries[PDPT_INDEX(vaddr)];
    }

    pd = (ptable_t *)PHYS_TO_VIRT(entry & PTE_ADDR_MASK);
    entry = pd->entries[PD_INDEX(vaddr)];
    if (!(entry & PTE_PRESENT))
    {
        phys = (uint64_t)pmm_alloc();
        if (!phys)
        {
            klog("paging", COL_AMBER "failed to allocate pt" COL_RESET);
            return -1;
        }
        pt = (ptable_t *)PHYS_TO_VIRT(phys);
        memset(pt, 0, PAGE_SIZE);
        pd->entries[PD_INDEX(vaddr)] = phys | PTE_PRESENT | PTE_RW;
        entry = pd->entries[PD_INDEX(vaddr)];
    }

    pt = (ptable_t *)PHYS_TO_VIRT(entry & PTE_ADDR_MASK);
    entry = pt->entries[PT_INDEX(vaddr)];
    if (entry & PTE_PRESENT)
    {
        klog("paging", COL_AMBER "remapping VA=%p (old PA=%p new PA=%p)" COL_RESET, vaddr, entry & PTE_ADDR_MASK, paddr);
    }

    pt->entries[PT_INDEX(vaddr)] = (paddr & PTE_ADDR_MASK) | flags | PTE_PRESENT;
    return 0;
}

int unmap_page(ptable_t *pml4, uint64_t vaddr)
{
    ptable_t *pdpt;
    ptable_t *pd;
    ptable_t *pt;
    uint64_t entry;

    entry = pml4->entries[PML4_INDEX(vaddr)];
    if (!(entry & PTE_PRESENT))
    {
        klog("paging", COL_AMBER "unmap: missing pml4 entry VA=%p" COL_RESET, vaddr);
        return -1;
    }

    pdpt = (ptable_t *)PHYS_TO_VIRT(entry & PTE_ADDR_MASK);
    entry = pdpt->entries[PDPT_INDEX(vaddr)];
    if (!(entry & PTE_PRESENT))
    {
        klog("paging", COL_AMBER "unmap: missing pdpt entry VA=%p" COL_RESET, vaddr);
        return -1;
    }

    pd = (ptable_t *)PHYS_TO_VIRT(entry & PTE_ADDR_MASK);
    entry = pd->entries[PD_INDEX(vaddr)];
    if (!(entry & PTE_PRESENT))
    {
        klog("paging", COL_AMBER "unmap: missing pd entry VA=%p" COL_RESET, vaddr);
        return -1;
    }

    pt = (ptable_t *)PHYS_TO_VIRT(entry & PTE_ADDR_MASK);
    entry = pt->entries[PT_INDEX(vaddr)];
    if (!(entry & PTE_PRESENT))
    {
        klog("paging", COL_AMBER "unmap: page not mapped VA=%p" COL_RESET, vaddr);
        return -1;
    }

    pt->entries[PT_INDEX(vaddr)] = 0;
    invlpg(vaddr);
    return 0;
}

ptable_t *ptable_create()
{
    void *page = pmm_alloc();
    if (!page)
    {
        klog("paging", COL_AMBER "failed to create ptable");
        return NULL;
    }

    ptable_t *pml4 = PHYS_TO_VIRT(page);
    memset(pml4, 0, PAGE_SIZE);
    if (kernel_ptable)
    {
        ptable_t *kpml4 = PHYS_TO_VIRT((uint64_t)kernel_ptable);
        for (int i = 256; i < 512; i++)
            pml4->entries[i] = kpml4->entries[i];
    }
    return page;
}

void ptable_load(ptable_t *pt)
{
    __asm__ volatile("mov %0, %%cr3" ::"r"((uint64_t)pt) : "memory");
}

void paging_init()
{
    kernel_ptable = ptable_create();
#define MAP_SECTION(vstart, vend, flags)                            \
    do                                                              \
    {                                                               \
        uint64_t _v = ALIGN_DOWN((uint64_t)(vstart), PAGE_SIZE);    \
        uint64_t _e = ALIGN_UP((uint64_t)(vend), PAGE_SIZE);        \
        for (; _v < _e; _v += PAGE_SIZE)                            \
        {                                                           \
            uint64_t _p = _v - kernel_virt + kernel_phys;           \
            map_page(PHYS_TO_VIRT(kernel_ptable), _v, _p, (flags)); \
        }                                                           \
    } while (0)

    MAP_SECTION(__limine_requests_start, __limine_requests_end, PTE_NX);
    MAP_SECTION(__text_start, __text_end, 0);
    MAP_SECTION(__rodata_start, __rodata_end, PTE_NX);
    MAP_SECTION(__data_start, __data_end, PTE_RW | PTE_NX);

    uint64_t fb_vaddr = (uint64_t)moose_fb->address;
    uint64_t fb_size = ALIGN_UP(moose_fb->pitch * moose_fb->height, PAGE_SIZE);

    for (uint64_t i = 0; i < fb_size; i += PAGE_SIZE)
    {
        uint64_t vaddr = fb_vaddr + i;
        uint64_t paddr = vaddr - kernel_virt + kernel_phys;
        map_page(PHYS_TO_VIRT(kernel_ptable), vaddr, paddr, PTE_RW | PTE_NX);
    }

    ptable_t *new_pml4 = (ptable_t *)PHYS_TO_VIRT((uint64_t)kernel_ptable);
    ptable_t *boot_pml4 = (ptable_t *)PHYS_TO_VIRT(read_cr3());

    for (int i = 256; i < 512; i++)
        new_pml4->entries[i] = boot_pml4->entries[i];

    klog("paging", "mappings:");
    klog("paging", "  limine_requests: 0x%llx -> 0x%llx [RO NX]", (uint64_t)__limine_requests_start, (uint64_t)__limine_requests_end);
    klog("paging", "  text:            0x%llx -> 0x%llx [RX]", (uint64_t)__text_start, (uint64_t)__text_end);
    klog("paging", "  rodata:          0x%llx -> 0x%llx [RO NX]", (uint64_t)__rodata_start, (uint64_t)__rodata_end);
    klog("paging", "  data/bss:        0x%llx -> 0x%llx [RW NX]", (uint64_t)__data_start, (uint64_t)__data_end);
    klog("paging", "  framebuffer:     0x%llx -> 0x%llx [RW NX]", fb_vaddr, fb_vaddr + fb_size);
    ptable_load(kernel_ptable);
}