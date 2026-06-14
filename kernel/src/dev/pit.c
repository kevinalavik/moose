#include <dev/pit.h>
#include <dev/tsc.h>
#include <arch/io.h>
#include <arch/idt.h>
#include <sys/irq.h>
#include <sys/apic.h>
#include <lib/printk.h>

#define PIT_CH0 0x40
#define PIT_CMD 0x43
#define PIT_FREQ 1193182

static volatile uint32_t pit_ticks;

static void pit_handler(int_frame_t *frame)
{
	(void)frame;
	pit_ticks++;
}

void pit_init(void)
{
	uint16_t divisor = PIT_FREQ / 1000;

	outb(PIT_CMD, 0x36);
	outb(PIT_CH0, divisor & 0xFF);
	outb(PIT_CH0, (divisor >> 8) & 0xFF);

	irq_register(0, pit_handler);

	printk("pit: initialized at %u Hz\n", PIT_FREQ / divisor);
}
