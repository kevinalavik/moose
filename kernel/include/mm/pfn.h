#ifndef MM_PFN_H
#define MM_PFN_H

#include <boot/limine.h>
#include <limine.h>
#include <stdint.h>

#define PAGE_SIZE 0x1000
#define PAGE_SHIFT 12

enum {
	PAGE_FREE = (1u << 0),
	PAGE_USED = (1u << 1),
	PAGE_RESERVED = (1u << 2),
};

typedef struct page {
	uint32_t flags;
	uint32_t refcount;
	struct page *next;
} page_t;

extern page_t *mem_map;
extern uint64_t max_pfn;

void pfn_init(struct limine_memmap_response *memmap);

static inline void *phys_to_virt(uint64_t phys)
{
	return (void *)(phys + hhdm_request.response->offset);
}

static inline uint64_t virt_to_phys(const void *virt)
{
	return (uint64_t)virt - hhdm_request.response->offset;
}

static inline page_t *pfn_to_page(uint64_t pfn)
{
	return &mem_map[pfn];
}

static inline uint64_t page_to_pfn(const page_t *page)
{
	return (uint64_t)(page - mem_map);
}

static inline uint64_t pfn_to_phys(uint64_t pfn)
{
	return pfn << PAGE_SHIFT;
}

static inline uint64_t phys_to_pfn(uint64_t phys)
{
	return phys >> PAGE_SHIFT;
}

static inline page_t *phys_to_page(uint64_t phys)
{
	return pfn_to_page(phys_to_pfn(phys));
}

static inline uint64_t page_to_phys(const page_t *page)
{
	return pfn_to_phys(page_to_pfn(page));
}

#endif // MM_PFN_H