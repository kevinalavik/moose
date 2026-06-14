#include <mm/ealloc.h>
#include <boot/limine.h>
#include <lib/string.h>
#include <lib/printk.h>
#include <sys/panic.h>
#include <stdint.h>
#include <mm/pfn.h>

struct ealloc_region {
	uint64_t base;
	uint64_t length;
};

static struct ealloc_region regions[EALLOC_MAX_REGIONS];
static size_t region_count;

static size_t cur_region;
static uint64_t cur_offset;

static struct ealloc_region reserved[EALLOC_MAX_RESERVED];
static size_t reserved_count;

static inline uintptr_t align_up(uintptr_t x, uintptr_t align)
{
	return (x + align - 1) & ~(align - 1);
}


void ealloc_init(struct limine_memmap_response *memmap)
{
	region_count = 0;
	cur_region = 0;
	cur_offset = 0;
	reserved_count = 0;

	for (uint64_t i = 0; i < memmap->entry_count; i++) {
		struct limine_memmap_entry *m = memmap->entries[i];

		if (m->type != LIMINE_MEMMAP_USABLE)
			continue;

		uint64_t base = m->base;
		uint64_t length = m->length;

		if (base < 0x100000) {
			if (base + length <= 0x100000)
				continue;

			length -= (0x100000 - base);
			base = 0x100000;
		}

		if (length == 0)
			continue;

		if (region_count >= EALLOC_MAX_REGIONS) {
			log("mm: ealloc: too many usable regions, ignoring the rest\n");
			break;
		}

		regions[region_count].base = base;
		regions[region_count].length = length;
		region_count++;
	}

	if (region_count == 0)
		panic(NULL, "failed to find usable memory for early allocator");

	log("mm: ealloc: %llu usable region(s) available for early allocation\n",
	       (unsigned long long)region_count);
}

static void ealloc_record(uint64_t phys, uint64_t size)
{
	if (reserved_count >= EALLOC_MAX_RESERVED) {
		log("mm: ealloc: too many early allocations to track, "
		       "pfn database reservations may be incomplete\n");
		return;
	}

	if (reserved_count > 0) {
		struct ealloc_region *prev = &reserved[reserved_count - 1];

		if (prev->base + prev->length == phys) {
			prev->length += size;
			return;
		}
	}

	reserved[reserved_count].base = phys;
	reserved[reserved_count].length = size;
	reserved_count++;
}

void *ealloc_aligned(size_t size, size_t align)
{
	if (size == 0)
		return NULL;

	while (cur_region < region_count) {
		struct ealloc_region *r = &regions[cur_region];

		uint64_t off = align_up(r->base + cur_offset, align) - r->base;

		if (off <= r->length && size <= r->length - off) {
			uint64_t phys = r->base + off;

			cur_offset = off + size;
			ealloc_record(phys, size);

			void *ptr = phys_to_virt(phys);

			memset(ptr, 0, size);
			return ptr;
		}

		cur_region++;
		cur_offset = 0;
	}

	panic(NULL, "early allocator exhausted");
	__builtin_unreachable();
}

void *ealloc(size_t size)
{
	return ealloc_aligned(size, 16);
}

size_t ealloc_reserved_count(void)
{
	return reserved_count;
}

void ealloc_reserved_range(size_t idx, uint64_t *base, uint64_t *length)
{
	*base = reserved[idx].base;
	*length = reserved[idx].length;
}