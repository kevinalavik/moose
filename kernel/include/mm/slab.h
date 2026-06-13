#ifndef MM_SLAB_H
#define MM_SLAB_H

#include <mm/pfn.h>
#include <stddef.h>
#include <stdint.h>

#define SLAB_MAGIC 0x42414c45534f4f4dULL /* "MOOSELAB" */
#define SLAB_MAX_CACHES 64

struct slab_cache;

typedef enum {
	SLAB_EMPTY,
	SLAB_PARTIAL,
	SLAB_FULL,
} slab_state_t;

typedef struct slab {
	uint64_t magic;
	struct slab *next;
	struct slab *prev;
	void *free_list;
	uint32_t free_count;
	uint32_t total_count;
	void *mem;
	slab_state_t state;
	struct slab_cache *cache;
	page_t *page;
} slab_t;

typedef struct slab_cache {
	const char *name;
	size_t obj_size;
	size_t obj_align;
	slab_t *partial;
	slab_t *full;
	slab_t *empty;
	void (*ctor)(void *);
	void (*dtor)(void *);
	int in_use;
} slab_cache_t;

slab_cache_t *slab_cache_create(
    const char *name, size_t size, size_t align, void (*ctor)(void *), void (*dtor)(void *));
void slab_cache_destroy(slab_cache_t *cache);
void *slab_cache_alloc(slab_cache_t *cache);
void slab_cache_free(slab_cache_t *cache, void *obj);
void slab_cache_reap(slab_cache_t *cache);

#endif // MM_SLAB_H