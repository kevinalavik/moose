#ifndef MM_LARGE_H
#define MM_LARGE_H

#include <mm/pfn.h>
#include <stddef.h>
#include <stdint.h>

#define LARGE_MAGIC 0x474942534f4f4dULL /* "MOOSBIG" */

struct large_header {
	uint64_t magic;
	page_t *first_page;
	size_t num_pages;
};

#define LARGE_MAX_SIZE (PAGE_SIZE - sizeof(struct large_header))

void *large_alloc(size_t size);
void large_free(void *ptr);

#endif // MM_LARGE_H