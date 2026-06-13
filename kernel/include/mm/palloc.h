#ifndef MM_PALLOC_H
#define MM_PALLOC_H

#include <mm/pfn.h>
#include <stddef.h>

void palloc_init(void);
page_t *palloc(void);
page_t *palloc_contiguous(size_t count);
void pfree(page_t *page);

#endif // MM_PALLOC_H