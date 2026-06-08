#ifndef MM_PMM_H
#define MM_PMM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 0x1000

#define PAGE_FREE (1 << 0)
#define PAGE_USED (1 << 1)
#define PAGE_RESERVED (1 << 3)

typedef struct page {
	struct page *next;
	uint64_t flags;
	uint32_t refcount;
} __attribute__((aligned(64))) page_t;

void pmm_init(void);

void *pmm_alloc(void);
void *pmm_alloc_contiguous(size_t count);
void pmm_free(void *ptr);

void pmm_ref(void *ptr);
void pmm_unref(void *ptr);

#endif /* MM_PMM_H */