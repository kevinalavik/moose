#ifndef ARCH_CPU_H
#define ARCH_CPU_H

__attribute__((noreturn)) static inline void hcf(void)
{
    asm volatile("cli");
    for (;;)
        asm volatile("hlt");
}

__attribute__((noreturn)) static inline void hlt(void)
{
    for (;;)
        asm volatile("hlt");
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" ::"a"(value), "Nd"(port));
}

static inline void sti()
{
    __asm__ volatile("sti");
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

static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

#endif /* ARCH_CPU_H */