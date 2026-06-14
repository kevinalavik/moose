#include <mm/pfn.h>
#include <mm/ealloc.h>
#include <lib/string.h>
#include <sys/panic.h>
#include <lib/printk.h>

page_t *mem_map = NULL;
uint64_t max_pfn = 0;

static uint64_t end_of_ram_pfn(struct limine_memmap_response *memmap)
{
	uint64_t highest = 0;

	for (uint64_t i = 0; i < memmap->entry_count; i++) {
		struct limine_memmap_entry *entry = memmap->entries[i];

		if (entry->type != LIMINE_MEMMAP_USABLE)
			continue;

		uint64_t end = entry->base + entry->length;

		if (end > highest)
			highest = end;
	}

	return (highest + PAGE_SIZE - 1) >> PAGE_SHIFT;
}

void pfn_init(struct limine_memmap_response *memmap)
{
	max_pfn = end_of_ram_pfn(memmap);

	if (max_pfn == 0)
		panic(NULL, "no usable memory found while building pfn database");

	mem_map = ealloc(max_pfn * sizeof(page_t));

	if (!mem_map)
		panic(NULL, "failed to allocate PFN database");

	for (uint64_t pfn = 0; pfn < max_pfn; pfn++) {
		mem_map[pfn].flags = PAGE_RESERVED;
		mem_map[pfn].refcount = 0;
		mem_map[pfn].next = NULL;
	}

	for (uint64_t i = 0; i < memmap->entry_count; i++) {
		struct limine_memmap_entry *entry = memmap->entries[i];

		if (entry->type != LIMINE_MEMMAP_USABLE)
			continue;

		uint64_t start_pfn = entry->base >> PAGE_SHIFT;
		uint64_t end_pfn = (entry->base + entry->length) >> PAGE_SHIFT;

		if (end_pfn > max_pfn)
			end_pfn = max_pfn;

		for (uint64_t pfn = start_pfn; pfn < end_pfn; pfn++)
			mem_map[pfn].flags = PAGE_FREE;
	}

	for (size_t i = 0; i < ealloc_reserved_count(); i++) {
		uint64_t base, length;

		ealloc_reserved_range(i, &base, &length);

		uint64_t start_pfn = base >> PAGE_SHIFT;
		uint64_t end_pfn = (base + length + PAGE_SIZE - 1) >> PAGE_SHIFT;

		if (end_pfn > max_pfn)
			end_pfn = max_pfn;

		for (uint64_t pfn = start_pfn; pfn < end_pfn; pfn++)
			mem_map[pfn].flags = PAGE_RESERVED;
	}

	uint64_t free_pages = 0;
	uint64_t reserved_pages = 0;

	for (uint64_t pfn = 0; pfn < max_pfn; pfn++) {
		if (mem_map[pfn].flags & PAGE_FREE)
			free_pages++;

		if (mem_map[pfn].flags & PAGE_RESERVED)
			reserved_pages++;
	}

	log("mm: PFN database @ %p (pfn 0..%llu)\n", mem_map, max_pfn);
	log("mm: max_pfn=%llu\n", max_pfn);
	log("mm: free_pages=%llu (%llu MiB)\n", free_pages, free_pages / 256);
	log("mm: reserved_pages=%llu\n", reserved_pages);
}