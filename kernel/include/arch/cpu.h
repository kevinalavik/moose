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

#endif /* ARCH_CPU_H */