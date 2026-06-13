#include <mm/large.h>
#include <mm/palloc.h>
#include <mm/pfn.h>
#include <sys/panic.h>
#include <stddef.h>

void *large_alloc(size_t size)
{
	if (size == 0)
		return NULL;

	size_t total = sizeof(struct large_header) + size;
	size_t npages = (total + PAGE_SIZE - 1) / PAGE_SIZE;

	page_t *first = palloc_contiguous(npages);
	if (!first)
		return NULL;

	uint64_t phys = page_to_phys(first);
	struct large_header *hdr = (struct large_header *)phys_to_virt(phys);

	hdr->magic = LARGE_MAGIC;
	hdr->first_page = first;
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

	uint64_t phys = page_to_phys(hdr->first_page);

	for (size_t i = 0; i < hdr->num_pages; i++) {
		page_t *p = phys_to_page(phys + i * PAGE_SIZE);
		pfree(p);
	}
}