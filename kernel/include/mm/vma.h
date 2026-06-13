#ifndef MM_VMA_H
#define MM_VMA_H

#include <stdint.h>
#include <stddef.h>

#include <mm/pfn.h>
#include <mm/palloc.h>
#include <arch/paging.h>

#define VMA_READ (1u << 0)
#define VMA_WRITE (1u << 1)
#define VMA_EXEC (1u << 2)

typedef enum {
	VMA_ANON = 0,
	VMA_PHYS,
} vma_type_t;

typedef struct vmap_proto {
	vma_type_t type;
	uintptr_t phys;
} vmap_proto_t;

#define VMPROTO_ANON ((vmap_proto_t){.type = VMA_ANON, .phys = 0})
#define VMPROTO_PHYS(addr) ((vmap_proto_t){.type = VMA_PHYS, .phys = (addr)})

typedef struct vma {
	uintptr_t start;
	uintptr_t end; /* exclusive */

	uint32_t prot;
	vma_type_t type;

	union vma_backing {
		void *unused;
		uintptr_t phys;
	} backing;

	struct vma *next;
	struct vma *prev;

} vma_t;

typedef struct {
	ptable_t *ptable;
	vma_t *vma_list;
	uintptr_t map_base;
	uintptr_t alloc_hint;
	vma_t *vfind_hint;
} vctx_t;


void vinit(vctx_t *ctx, ptable_t *ptable);
vma_t *vcreate(uintptr_t start, uintptr_t end, uint32_t prot, vmap_proto_t proto);
void vdestroy(vctx_t *ctx, vma_t *vma);
vma_t *vfind(vctx_t *ctx, uintptr_t addr);
uintptr_t vmap_anon(vctx_t *ctx, size_t size);
uintptr_t vmap_mmio(vctx_t *ctx, uintptr_t phys, size_t size);
void vunmap(vctx_t *ctx, uintptr_t addr, size_t size);
int vfault(vctx_t *ctx, uintptr_t addr, uint32_t access_flags);

extern vctx_t *kernel_vctx;

#endif // MM_VMA_H