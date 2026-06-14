#ifndef BOOT_LIMINE_H
#define BOOT_LIMINE_H

#include <stdint.h>
#include <limine.h>

extern volatile uint64_t limine_base_revision[];
extern volatile struct limine_framebuffer_request fb_request;
extern volatile struct limine_memmap_request memmap_request;
extern volatile struct limine_hhdm_request hhdm_request;
extern volatile struct limine_executable_address_request kaddr_request;
extern volatile struct limine_rsdp_request rsdp_request;
extern volatile struct limine_executable_cmdline_request cmdline_request;

#endif // BOOT_LIMINE_H