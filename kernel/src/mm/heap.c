#include "lib/math.h"
#include <mm/heap.h>
#include <mm/slab.h>
#include <mm/large.h>
#include <mm/pfn.h>
#include <lib/printk.h>
#include <sys/panic.h>
#include <stddef.h>
#include <stdint.h>

#define HEAP_NUM_CLASSES 8
static const size_t heap_size_classes[HEAP_NUM_CLASSES] = {
    16,
    32,
    64,
    128,
    256,
    512,
    1024,
    2048,
};

static slab_cache_t *heap_caches[HEAP_NUM_CLASSES];

void heap_init(void)
{
	for (int i = 0; i < HEAP_NUM_CLASSES; i++) {
		heap_caches[i] = slab_cache_create("kmalloc", heap_size_classes[i], 16, NULL, NULL);
		if (!heap_caches[i])
			panic(NULL, "heap: failed to create kmalloc cache (class %d)", i);
	}

	log("mm: kernel heap initialised (slab classes 16..%u bytes, large up to %u)\n",
	       heap_size_classes[HEAP_NUM_CLASSES - 1],
	       LARGE_MAX_SIZE);
}

static inline int size_class_index(size_t size)
{
	if (size <= 128) {
		if (size <= 32) {
			if (size <= 16)
				return 0;
			return 1;
		}
		if (size <= 64)
			return 2;
		return 3;
	}
	if (size <= 512) {
		if (size <= 256)
			return 4;
		return 5;
	}
	if (size > 2048)
		return -1;
	if (size <= 1024)
		return 6;
	return 7;
}

void *kmalloc(size_t size)
{
	if (size == 0)
		return NULL;

	int idx = size_class_index(size);
	if (idx >= 0)
		return slab_cache_alloc(heap_caches[idx]);

	return large_alloc(size);
}

void kfree(void *ptr)
{
	if (!ptr)
		return;

	uintptr_t page_addr = ALIGN_DOWN((uintptr_t)ptr, PAGE_SIZE);
	uint64_t magic = *(uint64_t *)page_addr;

	if (magic == SLAB_MAGIC) {
		slab_t *slab = (slab_t *)page_addr;
		slab_cache_free(slab->cache, ptr);
		return;
	}

	if (magic == LARGE_MAGIC) {
		large_free(ptr);
		return;
	}

	panic(NULL, "kfree: invalid pointer %p (bad magic, corruption or double free)", ptr);
}