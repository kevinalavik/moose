#ifndef SYS_MOOSE_H
#define SYS_MOOSE_H

#include <limine.h>
#include <util/bdf.h>
#include <stdint.h>

extern struct limine_framebuffer *moose_fb;
extern struct limine_memmap_response *moose_memmap;

extern uintptr_t moose_hhdm_off;
#define PHYS_TO_VIRT(p) ((void *)((uintptr_t)(p) + moose_hhdm_off))
#define VIRT_TO_PHYS(v) ((uintptr_t)(v) - moose_hhdm_off)
#define IS_HHDM(v) ((uintptr_t)(v) >= moose_hhdm_off)

extern BDF_Font moose_font;

#endif /* SYS_MOOSE_H */