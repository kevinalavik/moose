#include <mm/kheap.h>
#include <mm/pmm.h>
#include <sys/moose.h>
#include <sys/klog.h>
#include <lib/string.h>
#include <util/printf.h>
#include <lib/math.h>
#include <stdint.h>
#include <stddef.h>

#define HDR_MAGIC 0x434F4F4C
#define HDR_SZ sizeof(hdr_t)
#define MIN_BLOCK 64
#define HEAP_PAGES_PER_GROW 1

#define KMALLOC_PAGE_MAGIC 0x50414745

typedef struct {
	uint32_t magic;
	uint32_t npages;
} __attribute__((packed)) kmalloc_page_hdr_t;

typedef struct hdr {
	size_t size;
	uint32_t magic;
	int free;
	struct hdr *next;
	struct hdr *prev;
} hdr_t;

static hdr_t *root = NULL;
static void hdr_coalesce(hdr_t *h)
{
	if (h->next && h->next->free &&
	    (uint8_t *)h->next == (uint8_t *)(h + 1) + h->size) {
		h->size += HDR_SZ + h->next->size;
		h->next = h->next->next;
		if (h->next)
			h->next->prev = h;
	}

	if (h->prev && h->prev->free &&
	    (uint8_t *)h == (uint8_t *)(h->prev + 1) + h->prev->size) {
		h->prev->size += HDR_SZ + h->size;
		h->prev->next = h->next;
		if (h->next)
			h->next->prev = h->prev;
	}
}

static void hdr_insert(hdr_t *h)
{
	hdr_t **pp = &root;

	while (*pp && *pp < h)
		pp = &(*pp)->next;

	h->next = *pp;
	h->prev = NULL;
	if (*pp) {
		(*pp)->prev = h;
	}
	*pp = h;
	hdr_coalesce(h);
}

static int heap_grow(size_t min_size)
{
	size_t total = ALIGN_UP(min_size + HDR_SZ, PAGE_SIZE);
	size_t npages = total / PAGE_SIZE;
	if (npages < HEAP_PAGES_PER_GROW)
		npages = HEAP_PAGES_PER_GROW;

	void *phys = pmm_alloc_contiguous(npages);
	if (phys) {
		uint8_t *page = PHYS_TO_VIRT(phys);
		hdr_t *block = (hdr_t *)page;
		block->size = npages * PAGE_SIZE - HDR_SZ;
		block->magic = HDR_MAGIC;
		block->free = 1;
		hdr_insert(block);
		return 0;
	}

	uint8_t *base = NULL;
	uint64_t prev_phys = 0;
	size_t contiguous = 0;

	for (size_t i = 0; i < npages; i++) {
		phys = pmm_alloc();
		if (!phys) {
			for (size_t j = 0; j < i; j++)
				pmm_free((void *)((uint64_t)base -
						  moose_hhdm_off +
						  j * PAGE_SIZE));
			return -1;
		}

		uint8_t *page = PHYS_TO_VIRT(phys);
		if (!base) {
			base = page;
			prev_phys = (uint64_t)phys;
			contiguous = 1;
		} else if ((uint64_t)phys == prev_phys + PAGE_SIZE) {
			prev_phys = (uint64_t)phys;
			contiguous++;
		} else {
			hdr_t *block = (hdr_t *)base;
			block->size = contiguous * PAGE_SIZE - HDR_SZ;
			block->magic = HDR_MAGIC;
			block->free = 1;
			hdr_insert(block);
			base = page;
			prev_phys = (uint64_t)phys;
			contiguous = 1;
		}
	}

	hdr_t *block = (hdr_t *)base;
	block->size = contiguous * PAGE_SIZE - HDR_SZ;
	block->magic = HDR_MAGIC;
	block->free = 1;
	hdr_insert(block);
	return 0;
}

void *kmalloc(size_t size)
{
	if (!size)
		return NULL;

	if (size >= PAGE_SIZE) {
		size_t total = size + sizeof(kmalloc_page_hdr_t);
		size_t npages = ALIGN_UP(total, PAGE_SIZE) / PAGE_SIZE;
		void *phys = pmm_alloc_contiguous(npages);
		if (!phys)
			return NULL;
		kmalloc_page_hdr_t *hdr = PHYS_TO_VIRT(phys);
		hdr->magic = KMALLOC_PAGE_MAGIC;
		hdr->npages = npages;
		return (uint8_t *)hdr + sizeof(kmalloc_page_hdr_t);
	}

	size = (size + 7) & ~(size_t)7;
	for (int attempt = 0; attempt < 2; attempt++) {
		hdr_t *h = root;
		while (h) {
			if (h->free && h->magic == HDR_MAGIC &&
			    h->size >= size) {
				if (h->size >= size + HDR_SZ + MIN_BLOCK) {
					hdr_t *n =
						(hdr_t *)((uint8_t *)(h + 1) +
							  size);
					n->size = h->size - size - HDR_SZ;
					n->magic = HDR_MAGIC;
					n->free = 1;
					n->next = h->next;
					n->prev = h;
					if (h->next)
						h->next->prev = n;
					h->next = n;
					h->size = size;
				}

				h->free = 0;
				return (void *)(h + 1);
			}
			h = h->next;
		}
		if (attempt == 0) {
			if (heap_grow(size) != 0) {
				klog("kheap",
				     COL_BRED "heap_grow(%zu) failed" COL_RESET,
				     size);
				return NULL;
			}
		}
	}

	klog("kheap",
	     COL_AMBER "kmalloc(%zu) failed (no suitable block)" COL_RESET,
	     size);
	return NULL;
}

void kfree(void *ptr)
{
	if (!ptr)
		return;

	kmalloc_page_hdr_t *hdr =
		(kmalloc_page_hdr_t *)((uint8_t *)ptr -
				       sizeof(kmalloc_page_hdr_t));
	if ((uintptr_t)hdr % PAGE_SIZE == 0 &&
	    hdr->magic == KMALLOC_PAGE_MAGIC) {
		for (uint32_t i = 0; i < hdr->npages; i++)
			pmm_free((void *)((uintptr_t)hdr + i * PAGE_SIZE));
		return;
	}

	hdr_t *h = (hdr_t *)ptr - 1;
	if (h->magic != HDR_MAGIC || h->free) {
		klog("kheap",
		     COL_AMBER "kfree(%p): bad magic or double free" COL_RESET,
		     ptr);
		return;
	}

	h->free = 1;
	hdr_coalesce(h);
}
