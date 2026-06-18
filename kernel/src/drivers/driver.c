#include <drivers/driver.h>
#include <lib/printk.h>

extern driver_init_entry_t __driver_init_start[];
extern driver_init_entry_t __driver_init_end[];

void drivers_init(void)
{
	log("driver: initialising %zu drivers\n",
	    (size_t)(__driver_init_end - __driver_init_start));

	for (driver_init_entry_t *e = __driver_init_start; e < __driver_init_end; e++) {
		log("driver:   %s\n", e->name);
		e->init();
	}
}
