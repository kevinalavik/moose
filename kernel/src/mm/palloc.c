#include <mm/palloc.h>
#include <sys/panic.h>
#include <stddef.h>

static page_t *free_list = NULL;
static uint64_t free_page_count = 0;

void palloc_init(void)
{
	free_list = NULL;
	free_page_count = 0;

	for (uint64_t pfn = 0; pfn < max_pfn; pfn++) {
		page_t *page = pfn_to_page(pfn);

		if (!(page->flags & PAGE_FREE))
			continue;

		page->next = free_list;
		free_list = page;
		free_page_count++;
	}
}

page_t *palloc(void)
{
	while (free_list) {
		page_t *page = free_list;

		if (!(page->flags & PAGE_FREE)) {
			free_list = page->next;
			continue;
		}

		free_list = page->next;
		page->next = NULL;
		page->flags = PAGE_USED;
		page->refcount = 1;
		free_page_count--;
		return page;
	}

	return NULL;
}

page_t *palloc_contiguous(size_t count)
{
	if (count == 0)
		return NULL;

	if (count == 1)
		return palloc();

	for (uint64_t pfn = 0; pfn <= max_pfn - count; pfn++) {
		if (!(mem_map[pfn].flags & PAGE_FREE))
			continue;

		uint64_t start = pfn;
		int free_run = 1;

		for (uint64_t j = 1; j < count && start + j < max_pfn; j++) {
			if (mem_map[start + j].flags & PAGE_FREE)
				free_run++;
			else
				break;
		}

		if (free_run < (int)count) {
			pfn = start + free_run;
			continue;
		}

		for (uint64_t j = 0; j < count; j++) {
			page_t *p = &mem_map[start + j];
			p->flags = PAGE_USED;
			p->refcount = 1;
			p->next = NULL;
		}

		free_page_count -= count;
		return &mem_map[start];
	}

	return NULL;
}

void pfree(page_t *page)
{
	if (!page)
		return;

	if (page->flags & PAGE_RESERVED)
		panic(NULL, "attempted to free reserved page");

	if (!(page->flags & PAGE_USED))
		panic(NULL, "double free detected");

	page->flags = PAGE_FREE;
	page->refcount = 0;
	page->next = free_list;
	free_list = page;
	free_page_count++;
}