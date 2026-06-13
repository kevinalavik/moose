#include <dev/debugcon.h>
#include <arch/io.h>

void dbg_write(const char *str)
{
	while (*str) {
		outb(DEBUGCON_IO_PORT, *str++);
	}
}