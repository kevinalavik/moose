#ifndef ARCH_CPU_H
#define ARCH_CPU_H

/* generic x86_64 cpu helpers */

#include <arch/cpuid.h>
#include <lib/string.h>

/* generic instructions */
static inline void hlt()
{
	__asm__ volatile("hlt");
}

__attribute__((noreturn)) static inline void hcf()
{
	for (;;)
		hlt();
}

static inline void cli()
{
	__asm__ volatile("cli");
}
static inline void sti()
{
	__asm__ volatile("sti");
}

#define RFLAGS_IF (1 << 9)
static inline uint64_t read_flags()
{
	uint64_t flags;
	__asm__ volatile("pushfq\n\t"
	                 "popq %0"
	                 : "=r"(flags)
	                 :
	                 : "memory");
	return flags;
}

static inline void write_flags(uint64_t flags)
{
	__asm__ volatile("pushq %0\n\t"
	                 "popfq"
	                 :
	                 : "r"(flags)
	                 : "memory", "cc");
}
static inline uint64_t read_cr2(void)
{
	uint64_t cr2;
	__asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
	return cr2;
}

static inline uint64_t read_cr3(void)
{
	uint64_t cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
	return cr3;
}


static inline void invlpg(uint64_t addr)
{
	__asm__ volatile("invlpg (%0)" ::"r"(addr) : "memory");
}

/* generic helpers */
static inline char *get_cpu_string(void)
{
	static char cpu[128];
	uint32_t eax, ebx, ecx, edx;

	char vendor[13];
	char brand[49];

	cpuid(CPUID_VENDOR, 0, &eax, &ebx, &ecx, &edx);

	memcpy(vendor + 0, &ebx, 4);
	memcpy(vendor + 4, &edx, 4);
	memcpy(vendor + 8, &ecx, 4);
	vendor[12] = 0;

	cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);

	if (eax >= CPUID_BRAND2) {
		cpuid(CPUID_BRAND0,
		      0,
		      (uint32_t *)&brand[0],
		      (uint32_t *)&brand[4],
		      (uint32_t *)&brand[8],
		      (uint32_t *)&brand[12]);

		cpuid(CPUID_BRAND1,
		      0,
		      (uint32_t *)&brand[16],
		      (uint32_t *)&brand[20],
		      (uint32_t *)&brand[24],
		      (uint32_t *)&brand[28]);

		cpuid(CPUID_BRAND2,
		      0,
		      (uint32_t *)&brand[32],
		      (uint32_t *)&brand[36],
		      (uint32_t *)&brand[40],
		      (uint32_t *)&brand[44]);

		brand[48] = 0;
	} else {
		strcpy(brand, "Unknown CPU");
	}

	char *out = cpu;
	const char *s;
	char last = 0;

#define EMIT(c)                                                                                    \
	do {                                                                                       \
		char _c = (c);                                                                     \
		if (_c == ' ') {                                                                   \
			if (last != ' ')                                                           \
				*out++ = ' ', last = ' ';                                          \
		} else {                                                                           \
			*out++ = _c;                                                               \
			last = _c;                                                                 \
		}                                                                                  \
	} while (0)

	s = brand;
	while (*s)
		EMIT(*s++);

	EMIT(' ');
	EMIT('[');

	s = vendor;
	while (*s)
		EMIT(*s++);

	EMIT(']');
	EMIT('\0');

	return cpu;
}

/* MSR access — only use on CPUs known to support the specific MSR */
static inline uint64_t rdmsr(uint32_t msr)
{
	uint32_t lo, hi;
	__asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
	return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val)
{
	uint32_t lo = (uint32_t)val, hi = (uint32_t)(val >> 32);
	__asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr) : "memory");
}

/* TSC */
static inline uint64_t rdtsc(void)
{
	uint32_t lo, hi;
	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t rdtsc_ordered(void)
{
	uint32_t lo, hi;
	__asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi)::"memory");
	return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t rdtscp(uint32_t *aux)
{
	uint32_t lo, hi;
	__asm__ volatile("rdtscp" : "=a"(lo), "=d"(hi), "=c"(*aux));
	return ((uint64_t)hi << 32) | lo;
}

#include <arch/fastmem.h>

#endif // ARCH_CPU_H