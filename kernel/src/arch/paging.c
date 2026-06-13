#include <arch/paging.h>
#include <mm/palloc.h>
#include <mm/pfn.h>
#include <lib/string.h>
#include <arch/cpu.h>
#include <lib/printk.h>
#include <sys/panic.h>
#include <lib/math.h>

ptable_t *kernel_ptable = NULL;
extern char __limine_requests_start[];
extern char __limine_requests_end[];
extern char __text_start[];
extern char __text_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char __data_end[];
extern uint64_t kernel_phys;
extern uint64_t kernel_virt;

#define PT_MASK 0x1FFULL
#define PML4_INDEX(va) (((va) >> 39) & PT_MASK)
#define PDPT_INDEX(va) (((va) >> 30) & PT_MASK)
#define PD_INDEX(va) (((va) >> 21) & PT_MASK)
#define PT_INDEX(va) (((va) >> 12) & PT_MASK)
#define PAGE_OFFSET(va) ((va) & 0xFFFULL)

uint64_t ptable_virt_to_phys(ptable_t *pml4, uint64_t vaddr)
{
	ptable_t *pdpt;
	ptable_t *pd;
	ptable_t *pt;
	uint64_t entry;

	/* page map level 4 entry */
	entry = pml4->entries[PML4_INDEX(vaddr)];
	if (!(entry & PTE_PRESENT))
		return 0;

	pdpt = (ptable_t *)phys_to_virt(entry & PTE_ADDR_MASK);

	/* page directory pointer table entry (long ahh intel name) */
	entry = pdpt->entries[PDPT_INDEX(vaddr)];
	if (!(entry & PTE_PRESENT))
		return 0;

	pd = (ptable_t *)phys_to_virt(entry & PTE_ADDR_MASK);

	/* page directory entry */
	entry = pd->entries[PD_INDEX(vaddr)];
	if (!(entry & PTE_PRESENT))
		return 0;

	pt = (ptable_t *)phys_to_virt(entry & PTE_ADDR_MASK);

	/* page table entry */
	entry = pt->entries[PT_INDEX(vaddr)];
	if (!(entry & PTE_PRESENT))
		return 0;

	uint64_t paddr = entry & PTE_ADDR_MASK;
	return paddr + PAGE_OFFSET(vaddr);
}

int map_page(ptable_t *pml4, uint64_t vaddr, uint64_t paddr, uint64_t flags)
{
	ptable_t *pdpt;
	ptable_t *pd;
	ptable_t *pt;
	uint64_t entry;
	uint64_t phys;

	entry = pml4->entries[PML4_INDEX(vaddr)];
	if (!(entry & PTE_PRESENT)) {
		phys = page_to_phys(palloc());
		if (!phys) {
			panic(NULL, "failed to allocate pdpt");
			return -1;
		}

		pdpt = (ptable_t *)phys_to_virt(phys);
		memset(pdpt, 0, PAGE_SIZE);
		pml4->entries[PML4_INDEX(vaddr)] = phys | PTE_PRESENT | PTE_RW;
		entry = pml4->entries[PML4_INDEX(vaddr)];
	}

	pdpt = (ptable_t *)phys_to_virt(entry & PTE_ADDR_MASK);
	entry = pdpt->entries[PDPT_INDEX(vaddr)];
	if (!(entry & PTE_PRESENT)) {
		phys = page_to_phys(palloc());
		if (!phys) {
			panic(NULL, "failed to allocate pd");
			return -1;
		}

		pd = (ptable_t *)phys_to_virt(phys);
		memset(pd, 0, PAGE_SIZE);
		pdpt->entries[PDPT_INDEX(vaddr)] = phys | PTE_PRESENT | PTE_RW;
		entry = pdpt->entries[PDPT_INDEX(vaddr)];
	}

	pd = (ptable_t *)phys_to_virt(entry & PTE_ADDR_MASK);
	entry = pd->entries[PD_INDEX(vaddr)];
	if (!(entry & PTE_PRESENT)) {
		phys = page_to_phys(palloc());
		if (!phys) {
			panic(NULL, "failed to alloc pt");
			return -1;
		}
		pt = (ptable_t *)phys_to_virt(phys);
		memset(pt, 0, PAGE_SIZE);
		pd->entries[PD_INDEX(vaddr)] = phys | PTE_PRESENT | PTE_RW;
		entry = pd->entries[PD_INDEX(vaddr)];
	}

	pt = (ptable_t *)phys_to_virt(entry & PTE_ADDR_MASK);
	entry = pt->entries[PT_INDEX(vaddr)];
	if (entry & PTE_PRESENT) {
		if ((entry & PTE_ADDR_MASK) != (paddr & PTE_ADDR_MASK))
			printk("paging: remapping VA=%p (old PA=%p new PA=%p)\n",
			       vaddr,
			       entry & PTE_ADDR_MASK,
			       paddr);
	}

	pt->entries[PT_INDEX(vaddr)] = (paddr & PTE_ADDR_MASK) | flags | PTE_PRESENT;
	return 0;
}

int unmap_page(ptable_t *pml4, uint64_t vaddr)
{
	ptable_t *pdpt;
	ptable_t *pd;
	ptable_t *pt;
	uint64_t entry;

	entry = pml4->entries[PML4_INDEX(vaddr)];
	if (!(entry & PTE_PRESENT)) {
		printk("paging: unmap: missing pml4 entry VA=%p\n", vaddr);
		return -1;
	}

	pdpt = (ptable_t *)phys_to_virt(entry & PTE_ADDR_MASK);
	entry = pdpt->entries[PDPT_INDEX(vaddr)];
	if (!(entry & PTE_PRESENT)) {
		printk("paging: unmap: missing pdpt entry VA=%p\n", vaddr);
		return -1;
	}

	pd = (ptable_t *)phys_to_virt(entry & PTE_ADDR_MASK);
	entry = pd->entries[PD_INDEX(vaddr)];
	if (!(entry & PTE_PRESENT)) {
		printk("paging: unmap: missing pd entry VA=%p\n", vaddr);
		return -1;
	}

	pt = (ptable_t *)phys_to_virt(entry & PTE_ADDR_MASK);
	entry = pt->entries[PT_INDEX(vaddr)];
	if (!(entry & PTE_PRESENT)) {
		printk("paging: unmap: page not mapped VA=%p\n", vaddr);
		return -1;
	}

	pt->entries[PT_INDEX(vaddr)] = 0;
	invlpg(vaddr);
	return 0;
}

ptable_t *ptable_create()
{
	void *page = (void *)page_to_phys(palloc());
	if (!page) {
		panic(NULL, "failed to create new ptable");
	}

	ptable_t *pml4 = phys_to_virt((uint64_t)page);
	memset(pml4, 0, PAGE_SIZE);
	if (kernel_ptable) {
		ptable_t *kpml4 = phys_to_virt((uint64_t)kernel_ptable);
		for (int i = 256; i < 512; i++)
			pml4->entries[i] = kpml4->entries[i];
	}
	return page;
}

void ptable_load(ptable_t *pt)
{
	__asm__ volatile("mov %0, %%cr3" ::"r"((uint64_t)pt) : "memory");
}

void paging_init()
{
	kernel_ptable = ptable_create();
#define MAP_SECTION(vstart, vend, flags)                                                           \
	do {                                                                                       \
		uint64_t _v = ALIGN_DOWN((uint64_t)(vstart), PAGE_SIZE);                           \
		uint64_t _e = ALIGN_UP((uint64_t)(vend), PAGE_SIZE);                               \
		for (; _v < _e; _v += PAGE_SIZE) {                                                 \
			uint64_t _p = _v - kernel_virt + kernel_phys;                              \
			map_page(phys_to_virt((uint64_t)kernel_ptable), _v, _p, (flags));          \
		}                                                                                  \
	} while (0)

	MAP_SECTION(__limine_requests_start, __limine_requests_end, PTE_NX);
	MAP_SECTION(__text_start, __text_end, 0);
	MAP_SECTION(__rodata_start, __rodata_end, PTE_NX);
	MAP_SECTION(__data_start, __data_end, PTE_RW | PTE_NX);

	ptable_t *new_pml4 = (ptable_t *)phys_to_virt((uint64_t)kernel_ptable);
	ptable_t *boot_pml4 = (ptable_t *)phys_to_virt(read_cr3());

	for (int i = 256; i < 512; i++)
		new_pml4->entries[i] = boot_pml4->entries[i];

	printk("paging: mappings:\n");
	printk("paging:   limine_requests: 0x%llx -> 0x%llx [RO NX]\n",
	       (uint64_t)__limine_requests_start,
	       (uint64_t)__limine_requests_end);
	printk("paging:   text:            0x%llx -> 0x%llx [RX]\n",
	       (uint64_t)__text_start,
	       (uint64_t)__text_end);
	printk("paging:   rodata:          0x%llx -> 0x%llx [RO NX]\n",
	       (uint64_t)__rodata_start,
	       (uint64_t)__rodata_end);
	printk("paging:   data/bss:        0x%llx -> 0x%llx [RW NX]\n",
	       (uint64_t)__data_start,
	       (uint64_t)__data_end);
	ptable_load(kernel_ptable);
	printk("sys: loaded kernel CR3: %p\n", (void *)read_cr3());
}