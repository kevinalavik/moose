#include <mm/large.h>
#include <mm/palloc.h>
#include <mm/pfn.h>
#include <arch/paging.h>
#include <lib/string.h>
#include <lib/math.h>
#include <sys/panic.h>
#include <stddef.h>

#define LARGE_ARENA_BASE 0xFFFFA00000000000ULL

static uintptr_t large_arena_next = LARGE_ARENA_BASE;

static inline ptable_t *kernel_pml4(void)
{
	return (ptable_t *)phys_to_virt((uint64_t)kernel_ptable);
}

static uintptr_t large_arena_alloc(size_t num_pages)
{
	uintptr_t base = large_arena_next;
	size_t size = num_pages * PAGE_SIZE;

	if (large_arena_next + size < large_arena_next)
		panic(NULL, "large_alloc: virtual arena exhausted");

	large_arena_next += size;
	return base;
}

void *large_alloc(size_t size)
{
	if (size == 0)
		return NULL;

	size_t total = sizeof(struct large_header) + size;
	size_t npages = (total + PAGE_SIZE - 1) / PAGE_SIZE;

	uintptr_t base = large_arena_alloc(npages);

	for (size_t i = 0; i < npages; i++) {
		page_t *page = palloc();
		if (!page) {
			for (size_t j = 0; j < i; j++) {
				uintptr_t vaddr = base + j * PAGE_SIZE;
				uint64_t phys = ptable_virt_to_phys(kernel_pml4(), vaddr);

				unmap_page(kernel_pml4(), vaddr);
				if (phys)
					pfree(phys_to_page(phys));
			}

			return NULL;
		}

		uint64_t phys = page_to_phys(page);
		uintptr_t vaddr = base + i * PAGE_SIZE;

		if (map_page(kernel_pml4(), vaddr, phys, PTE_RW | PTE_NX) != 0)
			panic(NULL,
			      "large_alloc: failed to map %p -> %p",
			      (void *)vaddr,
			      (void *)phys);
	}

	struct large_header *hdr = (struct large_header *)base;

	hdr->magic = LARGE_MAGIC;
	hdr->base = base;
	hdr->num_pages = npages;

	return (void *)((uint8_t *)hdr + sizeof(struct large_header));
}

void large_free(void *ptr)
{
	struct large_header *hdr =
	    (struct large_header *)((uint8_t *)ptr - sizeof(struct large_header));

	if (hdr->magic != LARGE_MAGIC)
		panic(NULL, "large_free: bad magic (corruption or double free)");

	hdr->magic = 0;

	uintptr_t base = hdr->base;
	size_t npages = hdr->num_pages;

	for (size_t i = 0; i < npages; i++) {
		uintptr_t vaddr = base + i * PAGE_SIZE;
		uint64_t phys = ptable_virt_to_phys(kernel_pml4(), vaddr);

		if (!phys)
			panic(NULL, "large_free: missing mapping for %p", (void *)vaddr);

		unmap_page(kernel_pml4(), vaddr);
		pfree(phys_to_page(phys));
	}
}