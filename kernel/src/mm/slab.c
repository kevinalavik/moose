#include <mm/slab.h>
#include <mm/palloc.h>
#include <mm/pfn.h>
#include <lib/math.h>
#include <stddef.h>

static slab_cache_t cache_pool[SLAB_MAX_CACHES];

static slab_cache_t *alloc_cache_struct(void)
{
	for (int i = 0; i < SLAB_MAX_CACHES; i++) {
		if (!cache_pool[i].in_use) {
			cache_pool[i].in_use = 1;
			return &cache_pool[i];
		}
	}

	return NULL;
}

static void list_push_front(slab_t **head, slab_t *s)
{
	s->prev = NULL;
	s->next = *head;

	if (*head)
		(*head)->prev = s;

	*head = s;
}

static void list_remove(slab_t **head, slab_t *s)
{
	if (s->prev)
		s->prev->next = s->next;
	else
		*head = s->next;

	if (s->next)
		s->next->prev = s->prev;

	s->next = s->prev = NULL;
}

static slab_t **list_for_state(slab_cache_t *cache, slab_state_t state)
{
	switch (state) {
	case SLAB_EMPTY:
		return &cache->empty;
	case SLAB_PARTIAL:
		return &cache->partial;
	case SLAB_FULL:
		return &cache->full;
	}

	return NULL;
}

static void slab_move(slab_cache_t *cache, slab_t *slab, slab_state_t new_state)
{
	list_remove(list_for_state(cache, slab->state), slab);
	slab->state = new_state;
	list_push_front(list_for_state(cache, new_state), slab);
}

static slab_t *slab_create(slab_cache_t *cache)
{
	page_t *page = palloc();

	if (!page)
		return NULL;

	slab_t *slab = (slab_t *)phys_to_virt(page_to_phys(page));

	slab->magic = SLAB_MAGIC;
	slab->next = slab->prev = NULL;
	slab->cache = cache;
	slab->page = page;
	slab->state = SLAB_EMPTY;

	uintptr_t mem_addr = ALIGN_UP((uintptr_t)slab + sizeof(slab_t), cache->obj_align);
	slab->mem = (void *)mem_addr;

	size_t header_space = mem_addr - (uintptr_t)slab;
	size_t avail = PAGE_SIZE - header_space;
	slab->total_count = (uint32_t)(avail / cache->obj_size);
	slab->free_count = slab->total_count;

	uint8_t *obj = (uint8_t *)slab->mem;
	void *prev = NULL;

	for (uint32_t i = 0; i < slab->total_count; i++) {
		*(void **)obj = prev;
		prev = obj;
		obj += cache->obj_size;
	}

	slab->free_list = prev;

	return slab;
}

slab_cache_t *slab_cache_create(
    const char *name, size_t size, size_t align, void (*ctor)(void *), void (*dtor)(void *))
{
	if (align < sizeof(void *))
		align = sizeof(void *);

	size = ALIGN_UP(size, align);

	if (size + sizeof(slab_t) > PAGE_SIZE)
		return NULL;

	slab_cache_t *cache = alloc_cache_struct();

	if (!cache)
		return NULL;

	cache->name = name;
	cache->obj_size = size;
	cache->obj_align = align;
	cache->partial = cache->full = cache->empty = NULL;
	cache->ctor = ctor;
	cache->dtor = dtor;

	return cache;
}

void slab_cache_destroy(slab_cache_t *cache)
{
	slab_cache_reap(cache);
	cache->in_use = 0;
}

void *slab_cache_alloc(slab_cache_t *cache)
{
	slab_t *slab = cache->partial;

	if (!slab) {
		if (cache->empty) {
			slab = cache->empty;
			list_remove(&cache->empty, slab);
		} else {
			slab = slab_create(cache);

			if (!slab)
				return NULL;
		}

		slab->state = SLAB_PARTIAL;
		list_push_front(&cache->partial, slab);
	}

	void *obj = slab->free_list;
	slab->free_list = *(void **)obj;
	slab->free_count--;

	if (slab->free_count == 0)
		slab_move(cache, slab, SLAB_FULL);

	if (cache->ctor)
		cache->ctor(obj);

	return obj;
}

void slab_cache_free(slab_cache_t *cache, void *obj)
{
	if (cache->dtor)
		cache->dtor(obj);

	uintptr_t page_addr = (uintptr_t)obj & ~((uintptr_t)PAGE_SIZE - 1);
	slab_t *slab = (slab_t *)page_addr;

	*(void **)obj = slab->free_list;
	slab->free_list = obj;
	slab->free_count++;

	if (slab->free_count == slab->total_count)
		slab_move(cache, slab, SLAB_EMPTY);
	else if (slab->free_count == 1)
		slab_move(cache, slab, SLAB_PARTIAL);
}

void slab_cache_reap(slab_cache_t *cache)
{
	slab_t *slab = cache->empty;

	while (slab) {
		slab_t *next = slab->next;

		list_remove(&cache->empty, slab);
		pfree(slab->page);
		slab = next;
	}
}