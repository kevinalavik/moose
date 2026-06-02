#ifndef ARCH_CPU_H
#define ARCH_CPU_H

__attribute__((noreturn)) static inline void hcf(void)
{
    for (;;)
        asm volatile("hlt");
}

#endif /* ARCH_CPU_H */