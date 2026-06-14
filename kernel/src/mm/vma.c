#include <mm/vma.h>
#include <mm/heap.h>
#include <mm/palloc.h>
#include <mm/pfn.h>
#include <arch/paging.h>
#include <lib/printk.h>
#include <lib/string.h>
#include <lib/math.h>
#include <sys/panic.h>

vctx_t *kernel_vctx = NULL;

#define VMA_DEFAULT_BASE 0xFFFF900000000000ULL

static uint64_t vma_prot_to_pte_flags(uint32_t prot)
{
	uint64_t flags = 0;

	if (prot & VMA_WRITE)
		flags |= PTE_RW;

	if (!(prot & VMA_EXEC))
		flags |= PTE_NX;

	return flags;
}

static void vma_link(vctx_t *ctx, vma_t *vma)
{
	vma_t *prev = NULL;
	vma_t *cur = ctx->vma_list;

	while (cur && cur->start < vma->start) {
		prev = cur;
		cur = cur->next;
	}

	if (prev && prev->end > vma->start)
		panic(NULL,
		      "vma_link: new vma [%p-%p) overlaps existing vma [%p-%p)",
		      (void *)vma->start,
		      (void *)vma->end,
		      (void *)prev->start,
		      (void *)prev->end);

	if (cur && cur->start < vma->end)
		panic(NULL,
		      "vma_link: new vma [%p-%p) overlaps existing vma [%p-%p)",
		      (void *)vma->start,
		      (void *)vma->end,
		      (void *)cur->start,
		      (void *)cur->end);

	vma->prev = prev;
	vma->next = cur;

	if (prev)
		prev->next = vma;
	else
		ctx->vma_list = vma;

	if (cur)
		cur->prev = vma;
}

static void vma_unlink(vctx_t *ctx, vma_t *vma)
{
	if (ctx->vfind_hint == vma)
		ctx->vfind_hint = NULL;

	if (vma->prev)
		vma->prev->next = vma->next;
	else
		ctx->vma_list = vma->next;

	if (vma->next)
		vma->next->prev = vma->prev;

	vma->next = NULL;
	vma->prev = NULL;
}

static void vma_release_pages(vctx_t *ctx, vma_t *vma, uintptr_t start, uintptr_t end)
{
	for (uintptr_t addr = start; addr < end; addr += PAGE_SIZE) {
		uint64_t phys = ptable_virt_to_phys(ctx->ptable, addr);

		if (!phys)
			continue;

		if (unmap_page(ctx->ptable, addr) != 0)
			panic(NULL, "vma_release_pages: failed to unmap %p", (void *)addr);

		if (vma->type != VMA_PHYS)
			pfree(phys_to_page(phys));
	}
}

void vinit(vctx_t *ctx, ptable_t *ptable)
{
	if (!ctx)
		panic(NULL, "vinit: ctx is NULL");

	if (!ptable)
		panic(NULL, "vinit: ptable is NULL");

	ctx->ptable = ptable;
	ctx->vma_list = NULL;
	ctx->map_base = VMA_DEFAULT_BASE;
	ctx->alloc_hint = VMA_DEFAULT_BASE;
	ctx->vfind_hint = NULL;

	log("vma: context %p initialised (ptable=%p, map_base=%p)\n",
	       ctx,
	       ptable,
	       (void *)ctx->map_base);
}

vma_t *vcreate(uintptr_t start, uintptr_t end, uint32_t prot, vmap_proto_t proto)
{
	if (end <= start)
		panic(NULL, "vcreate: invalid range [%p-%p)", (void *)start, (void *)end);

	if (IS_NOT_ALIGNED(start, PAGE_SIZE) || IS_NOT_ALIGNED(end, PAGE_SIZE))
		panic(NULL, "vcreate: unaligned range [%p-%p)", (void *)start, (void *)end);

	switch (proto.type) {
	case VMA_ANON:
		break;
	case VMA_PHYS:
		break;
	default:
		panic(NULL, "vcreate: unknown vma type %d", (int)proto.type);
	}

	vma_t *vma = kmalloc(sizeof(vma_t));
	if (!vma)
		panic(NULL,
		      "vcreate: out of memory allocating vma_t for [%p-%p)",
		      (void *)start,
		      (void *)end);

	vma->start = start;
	vma->end = end;
	vma->prot = prot;
	vma->type = proto.type;
	switch (proto.type) {
	case VMA_ANON:
		vma->backing.unused = NULL;
		break;
	case VMA_PHYS:
		vma->backing.phys = proto.phys;
		break;
	}
	vma->next = NULL;
	vma->prev = NULL;
	return vma;
}

void vdestroy(vctx_t *ctx, vma_t *vma)
{
	if (!ctx)
		panic(NULL, "vdestroy: ctx is NULL");

	if (!vma)
		panic(NULL, "vdestroy: vma is NULL");

	vma_release_pages(ctx, vma, vma->start, vma->end);
	vma_unlink(ctx, vma);
	kfree(vma);
}

vma_t *vfind(vctx_t *ctx, uintptr_t addr)
{
	if (!ctx)
		panic(NULL, "vfind: ctx is NULL");

	vma_t *vma = ctx->vfind_hint;
	if (vma && addr >= vma->start && addr < vma->end)
		return vma;

	for (vma = ctx->vma_list; vma; vma = vma->next) {
		if (addr >= vma->start && addr < vma->end) {
			ctx->vfind_hint = vma;
			return vma;
		}
	}

	return NULL;
}

static uintptr_t vmap_alloc_addr(vctx_t *ctx, size_t size)
{
	uintptr_t addr = ctx->alloc_hint;
	for (vma_t *vma = ctx->vma_list; vma; vma = vma->next) {
		if (addr + size <= vma->start)
			break;
		if (addr < vma->end)
			addr = vma->end;
	}

	if (addr + size < addr)
		panic(NULL, "vmap: address space exhausted while mapping %u bytes", size);

	ctx->alloc_hint = addr + size;
	return addr;
}

uintptr_t vmap_anon(vctx_t *ctx, size_t size)
{
	if (!ctx)
		panic(NULL, "vmap_anon: ctx is NULL");

	if (size == 0)
		panic(NULL, "vmap_anon: requested size is 0");

	size = ALIGN_UP(size, PAGE_SIZE);

	uintptr_t addr = vmap_alloc_addr(ctx, size);
	vma_t *vma = vcreate(addr, addr + size, VMA_READ | VMA_WRITE, VMPROTO_ANON);
	vma_link(ctx, vma);
	return addr;
}

uintptr_t vmap_mmio(vctx_t *ctx, uintptr_t phys, size_t size)
{
	if (!ctx)
		panic(NULL, "vmap_mmio: ctx is NULL");

	if (size == 0)
		panic(NULL, "vmap_mmio: requested size is 0");

	uintptr_t phys_aligned = ALIGN_DOWN(phys, PAGE_SIZE);
	uintptr_t offset = phys - phys_aligned;
	size_t map_size = ALIGN_UP(offset + size, PAGE_SIZE);

	uintptr_t addr = vmap_alloc_addr(ctx, map_size);
	vma_t *vma =
	    vcreate(addr, addr + map_size, VMA_READ | VMA_WRITE, VMPROTO_PHYS(phys_aligned));
	vma_link(ctx, vma);

	/* pre-map all MMIO pages to avoid page faults on first access */
	uint64_t flags = PTE_RW | PTE_NX;
	for (size_t i = 0; i < map_size; i += PAGE_SIZE) {
		if (map_page(ctx->ptable, addr + i, phys_aligned + i, flags) != 0)
			panic(NULL,
			      "vmap_mmio: failed to map %p -> %p",
			      (void *)(addr + i),
			      (void *)(phys_aligned + i));
	}

	return addr + offset;
}

void vunmap(vctx_t *ctx, uintptr_t addr, size_t size)
{
	if (!ctx)
		panic(NULL, "vunmap: ctx is NULL");

	if (size == 0)
		return;

	uintptr_t start = ALIGN_DOWN(addr, PAGE_SIZE);
	uintptr_t end = ALIGN_UP(addr + size, PAGE_SIZE);

	vma_t *vma = ctx->vma_list;

	while (vma) {
		vma_t *next = vma->next;
		if (vma->end <= start || vma->start >= end) {
			vma = next;
			continue;
		}

		uintptr_t ov_start = vma->start > start ? vma->start : start;
		uintptr_t ov_end = vma->end < end ? vma->end : end;

		vma_release_pages(ctx, vma, ov_start, ov_end);

		if (ov_start == vma->start && ov_end == vma->end) {
			vma_unlink(ctx, vma);
			kfree(vma);
		} else if (ov_start == vma->start) {
			vma->start = ov_end;
		} else if (ov_end == vma->end) {
			vma->end = ov_start;
		} else {
			vmap_proto_t tail_proto = {.type = vma->type};
			if (vma->type == VMA_PHYS)
				tail_proto.phys = vma->backing.phys + (ov_end - vma->start);
			vma_t *tail = vcreate(ov_end, vma->end, vma->prot, tail_proto);
			vma->end = ov_start;
			tail->prev = vma;
			tail->next = vma->next;

			if (vma->next)
				vma->next->prev = tail;

			vma->next = tail;
		}

		vma = next;
	}
}

int vfault(vctx_t *ctx, uintptr_t addr, uint32_t access_flags)
{
	if (!ctx)
		panic(NULL, "vfault: ctx is NULL");

	vma_t *vma = vfind(ctx, addr);
	if (!vma) {
		log("vma: fault at %p: no vma covers this address\n", (void *)addr);
		return -1;
	}

	if ((access_flags & VMA_WRITE) && !(vma->prot & VMA_WRITE)) {
		log("vma: fault at %p: write access denied (vma prot=0x%x)\n",
		       (void *)addr,
		       vma->prot);
		return -1;
	}

	if ((access_flags & VMA_EXEC) && !(vma->prot & VMA_EXEC)) {
		log("vma: fault at %p: exec access denied (vma prot=0x%x)\n",
		       (void *)addr,
		       vma->prot);
		return -1;
	}

	uintptr_t page_addr = ALIGN_DOWN(addr, PAGE_SIZE);
	if (ptable_virt_to_phys(ctx->ptable, page_addr) != 0)
		return 0;

	switch (vma->type) {
	case VMA_ANON: {
		page_t *page = palloc();
		if (!page)
			panic(NULL,
			      "vfault: out of physical memory servicing fault at %p",
			      (void *)addr);

		uint64_t phys = page_to_phys(page);
		memset(phys_to_virt(phys), 0, PAGE_SIZE);

		uint64_t flags = vma_prot_to_pte_flags(vma->prot);

		if (map_page(ctx->ptable, page_addr, phys, flags) != 0) {
			pfree(page);
			panic(NULL,
			      "vfault: failed to map page %p -> %p",
			      (void *)page_addr,
			      (void *)phys);
		}
		return 0;
	}
	case VMA_PHYS: {
		uint64_t phys = vma->backing.phys + (page_addr - vma->start);
		uint64_t flags = vma_prot_to_pte_flags(vma->prot);

		if (map_page(ctx->ptable, page_addr, phys, flags) != 0)
			panic(NULL,
			      "vfault: failed to map %p -> %p",
			      (void *)page_addr,
			      (void *)phys);
		return 0;
	}
	default:
		panic(NULL,
		      "vfault: vma [%p-%p) has unhandled type %d",
		      (void *)vma->start,
		      (void *)vma->end,
		      (int)vma->type);
	}

	return -1;
}