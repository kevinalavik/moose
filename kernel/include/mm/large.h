#ifndef MM_LARGE_H
#define MM_LARGE_H

#include <mm/pfn.h>
#include <stddef.h>
#include <stdint.h>

#define LARGE_MAGIC 0x474942534f4f4dULL /* "MOOSBIG" */

struct large_header {
	uint64_t magic;
	uintptr_t base;   /* virtual base address of this allocation, including header */
	size_t num_pages; /* number of pages mapped, including the header page */
};

#define LARGE_MAX_SIZE (SIZE_MAX - PAGE_SIZE)

void *large_alloc(size_t size);
void large_free(void *ptr);

#endif // MM_LARGE_H