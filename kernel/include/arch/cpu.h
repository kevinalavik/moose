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

#endif /* ARCH_CPU_H */