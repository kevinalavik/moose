#ifndef SYS_SPINLOCK_H
#define SYS_SPINLOCK_H

#include <arch/cpu.h>

typedef struct {
	volatile int locked;
} spinlock_t;

static inline void spin_init(spinlock_t *lock)
{
	lock->locked = 0;
}

static inline void spin_lock(spinlock_t *lock)
{
	while (__sync_lock_test_and_set(&lock->locked, 1))
		while (lock->locked)
			;
}

static inline void spin_unlock(spinlock_t *lock)
{
	__sync_lock_release(&lock->locked);
}

static inline unsigned long spin_lock_irqsave(spinlock_t *lock)
{
	unsigned long flags;
	__asm__ volatile("pushfq; popq %0; cli" : "=r"(flags)::"memory");
	spin_lock(lock);
	return flags;
}

static inline void spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
	spin_unlock(lock);
	if (flags & 0x200)
		sti();
}

#endif // SYS_SPINLOCK_H
