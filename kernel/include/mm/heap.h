#ifndef MM_HEAP_H
#define MM_HEAP_H

/* moose kernel heap (slab/large) */

#include <stddef.h>

void heap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);

#endif // MM_HEAP_H