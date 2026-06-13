#ifndef ARCH_PAGING_H
#define ARCH_PAGING_H

#include <stdint.h>

// bunch off flags from the osdev wiki,idf i just get present,rw,user
#define PTE_PRESENT (1ULL << 0)
#define PTE_RW (1ULL << 1)
#define PTE_USER (1ULL << 2)
#define PTE_PWT (1ULL << 3)
#define PTE_PCD (1ULL << 4)
#define PTE_ACCESSED (1ULL << 5)
#define PTE_DIRTY (1ULL << 6)
#define PTE_HUGE (1ULL << 7)
#define PTE_NX (1ULL << 63)

#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

typedef uint64_t pte_t;

typedef struct {
	pte_t entries[512];
} ptable_t;

extern ptable_t *kernel_ptable;

uint64_t ptable_virt_to_phys(ptable_t *pml4, uint64_t vaddr);
int map_page(ptable_t *pml4,
             uint64_t vaddr,
             uint64_t paddr,
             uint64_t flags); /* doesnt need PRESENT flag */
int unmap_page(ptable_t *pml4, uint64_t vaddr);
void paging_init();
ptable_t *ptable_create();

#endif // ARCH_PAGING_H