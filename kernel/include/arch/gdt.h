#ifndef ARCH_GDT_H
#define ARCH_GDT_H

#include <stdint.h>

typedef struct {
	uint16_t size;
	uintptr_t base;
} __attribute__((packed)) gdt_r;

typedef struct {
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t base_mid;
	uint8_t access;
	uint8_t limit : 4;
	uint8_t flags : 4;
	uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
	gdt_entry_t entries[7]; /* null, kcode, kdata, udata, ucode, tss_low, tss_high*/
} __attribute__((packed)) gdt_t;

#define GDT_ACCESS_PRESENT 0x80
#define GDT_ACCESS_RING0 0x00
#define GDT_ACCESS_RING3 0x60
#define GDT_ACCESS_CODE_OR_DATA 0x10
#define GDT_ACCESS_EXECUTABLE 0x08
#define GDT_ACCESS_RW 0x02
#define GDT_FLAGS_GRAN_4K 0x80
#define GDT_FLAGS_LONG_MODE 0x20
#define GDT_KCODE_SEL 0x08
#define GDT_KDATA_SEL 0x10

void gdt_init();

#endif // ARCH_GDT_H