#include <mm/vma.h>
#include <sys/klog.h>
#include <mm/kheap.h>
#include <lib/math.h>
#include <lib/string.h>
#include <sys/moose.h>

vctx_t *current_vctx = NULL;

static vma_t *vma_alloc_node(void)
{
    vma_t *v = kmalloc(sizeof(vma_t));
    if (!v)
        klog("vma", COL_AMBER "failed to allocate vma node" COL_RESET);
    else
        memset(v, 0, sizeof(vma_t));
    return v;
}

static void vma_insert(vctx_t *ctx, vma_t *v)
{
    vma_t **p = &ctx->vma_list;
    while (*p && (*p)->start < v->start)
        p = &(*p)->next;

    v->next = *p;
    v->prev = NULL;
    if (*p)
        (*p)->prev = v;
    *p = v;
}

static void vma_remove(vctx_t *ctx, vma_t *v)
{
    if (v->prev)
        v->prev->next = v->next;
    else
        ctx->vma_list = v->next;

    if (v->next)
        v->next->prev = v->prev;
}

static int vma_overlap(vma_t *a, uint64_t start, uint64_t end)
{
    return a->start < end && a->end > start;
}

static void vma_unmap_range(vctx_t *ctx, uint64_t start, uint64_t end)
{
    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE)
    {
        uint64_t phys = virt_to_phys(ctx->ptable, addr);
        if (phys)
        {
            unmap_page(ctx->ptable, addr);
            pmm_free((void *)ALIGN_DOWN(phys, PAGE_SIZE));
        }
    }
}

void vma_init(vctx_t *ctx, ptable_t *pt)
{
    ctx->vma_list = NULL;
    ctx->ptable = pt;
}

vma_t *vma_find(vctx_t *ctx, uint64_t addr)
{
    vma_t *v = ctx->vma_list;
    while (v)
    {
        if (addr >= v->start && addr < v->end)
            return v;
        v = v->next;
    }
    return NULL;
}

int vma_map_anon(vctx_t *ctx, uint64_t addr, uint64_t size, uint32_t prot, uint32_t flags)
{
    (void)flags; /* todo */
    uint64_t start = ALIGN_DOWN(addr, PAGE_SIZE);
    uint64_t end = ALIGN_UP(addr + size, PAGE_SIZE);

    if (end <= start || end - start > 0x100000000ULL)
        return -1;

    vma_t *existing = ctx->vma_list;
    while (existing)
    {
        if (vma_overlap(existing, start, end))
        {
            klog("vma", COL_AMBER "map_anon: overlap %p-%p with %p-%p" COL_RESET, start, end, existing->start, existing->end);
            return -1;
        }
        existing = existing->next;
    }

    vma_t *v = vma_alloc_node();
    if (!v)
        return -1;

    vm_object_t *obj = kmalloc(sizeof(vm_object_t));
    if (!obj)
    {
        klog("vma", COL_AMBER "failed to allocate vm_object" COL_RESET);
        kfree(v);
        return -1;
    }

    obj->type = VM_OBJ_ANON;
    v->start = start;
    v->end = end;
    v->prot = prot;
    v->flags = flags;
    v->obj = obj;
    vma_insert(ctx, v);
    return 0;
}

int vma_map_phys(vctx_t *ctx, uint64_t vaddr, uint64_t phys, uint64_t size, uint32_t prot, uint32_t flags)
{
    (void)flags; /* todo*/
    uint64_t start = ALIGN_DOWN(vaddr, PAGE_SIZE);
    uint64_t phys_start = ALIGN_DOWN(phys, PAGE_SIZE);
    uint64_t end = ALIGN_UP(vaddr + size, PAGE_SIZE);

    if (end <= start || end - start > 0x100000000ULL)
        return -1;

    vma_t *existing = ctx->vma_list;
    while (existing)
    {
        if (vma_overlap(existing, start, end))
        {
            klog("vma", COL_AMBER "map_phys: overlap %p-%p with %p-%p" COL_RESET, start, end, existing->start, existing->end);
            return -1;
        }
        existing = existing->next;
    }

    vma_t *v = vma_alloc_node();
    if (!v)
        return -1;

    vm_object_t *obj = kmalloc(sizeof(vm_object_t));
    if (!obj)
    {
        klog("vma", COL_AMBER "failed to allocate vm_object" COL_RESET);
        kfree(v);
        return -1;
    }

    obj->type = VM_OBJ_PHYS;
    obj->data = (void *)phys_start;
    v->start = start;
    v->end = end;
    v->prot = prot;
    v->flags = flags;
    v->obj = obj;

    uint64_t page_flags = 0;
    if (prot & VMA_PROT_WRITE)
        page_flags |= PTE_RW;
    if (!(prot & VMA_PROT_EXEC))
        page_flags |= PTE_NX;

    for (uint64_t off = 0; off < (end - start); off += PAGE_SIZE)
    {
        if (map_page(ctx->ptable, start + off, phys_start + off, page_flags) != 0)
        {
            klog("vma", COL_AMBER "map_phys: map_page failed at %p" COL_RESET, start + off);
            kfree(obj);
            kfree(v);
            return -1;
        }
    }

    vma_insert(ctx, v);
    return 0;
}

int vma_unmap(vctx_t *ctx, uint64_t addr, uint64_t size)
{
    uint64_t start = ALIGN_DOWN(addr, PAGE_SIZE);
    uint64_t end = ALIGN_UP(addr + size, PAGE_SIZE);

    if (end <= start)
        return -1;

    vma_t *v = ctx->vma_list;
    while (v)
    {
        vma_t *next = v->next;

        if (vma_overlap(v, start, end))
        {
            if (start <= v->start && end >= v->end)
            {
                vma_unmap_range(ctx, v->start, v->end);
                vma_remove(ctx, v);
                if (v->obj)
                    kfree(v->obj);
                kfree(v);
            }
            else if (start <= v->start)
            {
                vma_unmap_range(ctx, v->start, end);
                v->start = end;
            }
            else if (end >= v->end)
            {
                vma_unmap_range(ctx, start, v->end);
                v->end = start;
            }
            else
            {
                vma_unmap_range(ctx, start, end);

                vma_t *new_v = vma_alloc_node();
                if (new_v)
                {
                    new_v->start = end;
                    new_v->end = v->end;
                    new_v->prot = v->prot;
                    new_v->flags = v->flags;

                    vm_object_t *new_obj = kmalloc(sizeof(vm_object_t));
                    if (new_obj)
                    {
                        *new_obj = *v->obj;
                        new_v->obj = new_obj;
                    }
                    else
                    {
                        new_v->obj = v->obj;
                    }

                    new_v->next = v->next;
                    new_v->prev = v;
                    if (v->next)
                        v->next->prev = new_v;
                    v->next = new_v;
                    v->end = start;
                }
            }
        }
        v = next;
    }

    return 0;
}

int vma_handle_fault(vctx_t *ctx, uint64_t addr, bool is_write, bool is_user)
{
    (void)is_user; /* no usermode yet so only placeholder for now */
    vma_t *v = vma_find(ctx, addr);
    if (!v)
        return -1;

    uint64_t fault_page = ALIGN_DOWN(addr, PAGE_SIZE);

    if (is_write && !(v->prot & VMA_PROT_WRITE))
    {
        klog("vma", COL_BRED "fault: write to read-only VMA %p-%p at %p" COL_RESET, v->start, v->end, addr);
        return -1;
    }

    if (v->obj->type == VM_OBJ_ANON)
    {
        void *phys = pmm_alloc();
        if (!phys)
        {
            klog("vma", COL_BRED "fault: OOM for anon page at %p" COL_RESET, addr);
            return -1;
        }

        uint64_t page_flags = 0;
        if (v->prot & VMA_PROT_WRITE)
            page_flags |= PTE_RW;
        if (!(v->prot & VMA_PROT_EXEC))
            page_flags |= PTE_NX;

        memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
        if (map_page(ctx->ptable, fault_page, (uint64_t)phys, page_flags) != 0)
        {
            klog("vma", COL_AMBER "fault: map_page failed for anon %p" COL_RESET, fault_page);
            pmm_free(phys);
            return -1;
        }
    }
    else if (v->obj->type == VM_OBJ_PHYS)
    {
        klog("vma", COL_BRED "fault: unexpected fault in phys VMA %p-%p at %p" COL_RESET, v->start, v->end, addr);
        return -1;
    }

    return 0;
}
