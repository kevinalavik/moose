#ifndef MM_EALLOC_H
#define MM_EALLOC_H

/* mooses early allocator for getting the PFN db going */
#include <stddef.h>
#include <stdint.h>

struct limine_memmap_response;

#define EALLOC_MAX_REGIONS 64

void ealloc_init(struct limine_memmap_response *memmap);
void *ealloc(size_t size);
void *ealloc_aligned(size_t size, size_t align);
size_t ealloc_reserved_count(void);
void ealloc_reserved_range(size_t idx, uint64_t *base, uint64_t *length);

#endif // MM_EALLOC_H