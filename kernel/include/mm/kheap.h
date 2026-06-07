#ifndef MM_KHEAP_H
#define MM_KHEAP_H

#include <stddef.h>

void *kmalloc(size_t size);
void kfree(void *ptr);

#endif
