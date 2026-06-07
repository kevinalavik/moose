#ifndef MM_VMA_H
#define MM_VMA_H

#include <stdint.h>
#include <arch/paging.h>
#include <mm/pmm.h>

#define VMA_PROT_READ (1 << 0)
#define VMA_PROT_WRITE (1 << 1)
#define VMA_PROT_EXEC (1 << 2)

#define VMA_FLAG_SHARED (1 << 0) /* todo */
#define VMA_FLAG_FIXED (1 << 1)

typedef enum
{
    VM_OBJ_ANON = 0,
    VM_OBJ_PHYS
} vm_object_type_t;

typedef struct vm_object
{
    vm_object_type_t type;
    union
    {
        struct
        {
            int placeholder;
        } anon;
        struct
        {
            uint64_t phys_base;
        } phys;
    };
} vm_object_t;

typedef struct vma
{
    uint64_t start;
    uint64_t end;

    uint32_t prot;  /* VMA_PROTs */
    uint32_t flags; /* VMA_FLAGs*/

    vm_object_t *obj;

    struct vma *next;
    struct vma *prev;
} vma_t; /* virtual memory areas */

typedef struct vctx
{
    vma_t *vma_list;
    ptable_t *ptable;
} vctx_t; /* per process context */

void vma_init(vctx_t *ctx, ptable_t *pt);
vma_t *vma_find(vctx_t *ctx, uint64_t addr);
int vma_map_anon(vctx_t *ctx, uint64_t addr, uint64_t size, uint32_t prot, uint32_t flags);
int vma_map_phys(vctx_t *ctx, uint64_t vaddr, uint64_t phys, uint64_t size, uint32_t prot, uint32_t flags);
int vma_unmap(vctx_t *ctx, uint64_t addr, uint64_t size);
int vma_handle_fault(vctx_t *ctx, uint64_t addr, bool is_write, bool is_user);

#endif /* MM_VMA_H */