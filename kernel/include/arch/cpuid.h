#ifndef ARCH_CPUID_H
#define ARCH_CPUID_H

#include <stdint.h>

#define CPUID_VENDOR 0x00000000
#define CPUID_FEATURES 0x00000001
#define CPUID_EXT_FEATURES 0x00000007

#define CPUID_BRAND0 0x80000002
#define CPUID_BRAND1 0x80000003
#define CPUID_BRAND2 0x80000004

#define CPUID_FEAT_EDX_APIC (1 << 9)

static inline void
cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
	__asm__ volatile("cpuid"
	                 : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
	                 : "a"(leaf), "c"(subleaf));
}


#endif // ARCH_CPUID_H