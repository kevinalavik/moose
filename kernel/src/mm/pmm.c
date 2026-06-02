#include <mm/pmm.h>
#include <sys/moose.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/klog.h>
#include <lib/term.h>
#include <lib/math.h>

page_t *root = NULL;
static page_t *tail = NULL;
static page_t *mem_map = NULL;
static uint64_t total_pages = 0;
static uint64_t free_pages = 0;
static uint64_t used_pages = 0;
static uint64_t reserved_pages = 0;

static void _append_page(page_t *p)
{
    p->next = NULL;
    if (root == NULL)
    {
        root = p;
        tail = p;
        return;
    }
    tail->next = p;
    tail = p;
}

static uint64_t _find_map_region(uint64_t needed)
{
    uint64_t best_base = (uint64_t)-1;
    uint64_t best_size = 0;

    for (uint64_t i = 0; i < moose_memmap->entry_count; i++)
    {
        struct limine_memmap_entry *e = moose_memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE)
            continue;

        uint64_t base = ALIGN_UP(e->base, PAGE_SIZE);
        uint64_t end = ALIGN_DOWN(e->base + e->length, PAGE_SIZE);
        if (end <= base)
            continue;

        uint64_t size = end - base;
        if (size >= needed && size > best_size)
        {
            best_base = base;
            best_size = size;
        }
    }

    return best_base;
}

static page_t *_phys_to_page(uintptr_t phys)
{
    uint64_t pfn = phys / PAGE_SIZE;
    if (pfn >= total_pages)
        return NULL;
    return &mem_map[pfn];
}

void pmm_init(void)
{
    root = NULL;
    tail = NULL;
    total_pages = 0;
    free_pages = 0;
    used_pages = 0;
    reserved_pages = 0;

    uint64_t highest = 0;
    for (uint64_t i = 0; i < moose_memmap->entry_count; i++)
    {
        struct limine_memmap_entry *e = moose_memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE)
            continue;
        uint64_t end = e->base + e->length;
        if (end > highest)
            highest = end;
    }

    total_pages = ALIGN_UP(highest, PAGE_SIZE) / PAGE_SIZE;

    uint64_t map_bytes = total_pages * sizeof(page_t);
    uint64_t map_pages = ALIGN_UP(map_bytes, PAGE_SIZE) / PAGE_SIZE;
    uint64_t map_phys = _find_map_region(map_pages * PAGE_SIZE);

    if (map_phys == (uint64_t)-1)
    {
        klog("pmm", ANSI_RED "could not place page database (need %u MiB)" ANSI_RESET, (map_pages * PAGE_SIZE) / 1024 / 1024);
        return;
    }

    mem_map = (page_t *)PHYS_TO_VIRT(map_phys);

    klog("pmm", ANSI_CYAN "page database @ %p (%u pages, %u MiB)" ANSI_RESET, mem_map, map_pages, (map_pages * PAGE_SIZE) / 1024 / 1024);

    for (uint64_t i = 0; i < total_pages; i++)
    {
        mem_map[i].next = NULL;
        mem_map[i].flags = PAGE_RESERVED;
        mem_map[i].refcount = 0;
    }
    reserved_pages = total_pages;

    uint64_t map_end_phys = map_phys + map_pages * PAGE_SIZE;

    for (uint64_t i = 0; i < moose_memmap->entry_count; i++)
    {
        struct limine_memmap_entry *e = moose_memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE)
            continue;

        uint64_t start = ALIGN_UP(e->base, PAGE_SIZE);
        uint64_t end = ALIGN_DOWN(e->base + e->length, PAGE_SIZE);
        if (end <= start)
            continue;

        for (uint64_t phys = start; phys < end; phys += PAGE_SIZE)
        {
            uint64_t pfn = phys / PAGE_SIZE;
            if (pfn >= total_pages)
                continue;

            if (phys >= map_phys && phys < map_end_phys)
                continue;

            page_t *p = &mem_map[pfn];
            p->flags = PAGE_FREE;
            p->refcount = 0;
            p->next = NULL;
            _append_page(p);
            free_pages++;
            reserved_pages--;
        }
    }

    klog("pmm", ANSI_CYAN "%u MiB free, %u MiB reserved" ANSI_RESET, (free_pages * PAGE_SIZE) / 1024 / 1024, (reserved_pages * PAGE_SIZE) / 1024 / 1024);
}

void *pmm_alloc(void)
{
    if (root == NULL)
    {
        klog("pmm", ANSI_RED "out of physical memory" ANSI_RESET);
        return NULL;
    }

    page_t *p = root;
    root = root->next;
    if (root == NULL)
        tail = NULL;

    if (!(p->flags & PAGE_FREE))
    {
        klog("pmm", ANSI_RED "free list corruption @ %p" ANSI_RESET, p);
        return NULL;
    }

    uint64_t pfn = (uint64_t)(p - mem_map);
    uint64_t phys = pfn * PAGE_SIZE;

    p->next = NULL;
    p->flags = PAGE_USED;
    p->refcount = 0; /* its not managed else where other than the pmm, we dont need to show it in refcount since we got the magic of our page database :^)*/
    free_pages--;
    used_pages++;

    return (void *)phys;
}

void pmm_ref(void *ptr)
{
    if (ptr == NULL)
        return;

    uintptr_t phys = IS_HHDM(ptr) ? VIRT_TO_PHYS(ptr) : (uintptr_t)ptr;
    page_t *p = _phys_to_page(phys);

    if (p == NULL)
    {
        klog("pmm", ANSI_YELLOW "pmm_ref on out-of-range page @ %p, ignored" ANSI_RESET, ptr);
        return;
    }

    if (!(p->flags & PAGE_USED))
    {
        klog("pmm", ANSI_YELLOW "pmm_ref on non-used page @ %p, ignored" ANSI_RESET, ptr);
        return;
    }

    p->refcount++;
}

void pmm_unref(void *ptr)
{
    if (ptr == NULL)
        return;

    uintptr_t phys = IS_HHDM(ptr) ? VIRT_TO_PHYS(ptr) : (uintptr_t)ptr;
    page_t *p = _phys_to_page(phys);

    if (p == NULL)
    {
        klog("pmm", ANSI_YELLOW "pmm_unref on out-of-range page @ %p, ignored" ANSI_RESET, ptr);
        return;
    }

    if (!(p->flags & PAGE_USED))
    {
        klog("pmm", ANSI_YELLOW "pmm_unref on non-used page @ %p, ignored" ANSI_RESET, ptr);
        return;
    }

    if (p->refcount == 0)
    {
        klog("pmm", ANSI_YELLOW "pmm_unref on zero-refcount page @ %p, ignored" ANSI_RESET, ptr);
        return;
    }

    p->refcount--;
}

void pmm_free(void *ptr)
{
    if (ptr == NULL)
        return;

    if (IS_NOT_ALIGNED((uintptr_t)ptr, PAGE_SIZE))
    {
        klog("pmm", ANSI_YELLOW "tried to free unaligned page @ %p, ignored" ANSI_RESET, ptr);
        return;
    }

    uintptr_t phys = IS_HHDM(ptr) ? VIRT_TO_PHYS(ptr) : (uintptr_t)ptr;
    page_t *p = _phys_to_page(phys);

    if (p == NULL)
    {
        klog("pmm", ANSI_YELLOW "tried to free outside PMM range @ %p, ignored" ANSI_RESET, ptr);
        return;
    }

    if (p->flags & PAGE_RESERVED)
    {
        klog("pmm", ANSI_YELLOW "tried to free reserved page @ %p, ignored" ANSI_RESET, ptr);
        return;
    }

    if (!(p->flags & PAGE_USED))
    {
        klog("pmm", ANSI_YELLOW "tried to free non-used page @ %p, ignored" ANSI_RESET, ptr);
        return;
    }

    if (p->refcount > 0)
    {
        klog("pmm", ANSI_YELLOW "tried to free referenced page @ %p (refcount=%u), ignored" ANSI_RESET,
             ptr, p->refcount);
        return;
    }

    p->flags = PAGE_FREE;
    p->next = NULL;
    _append_page(p);
    free_pages++;
    if (used_pages > 0)
        used_pages--;
}