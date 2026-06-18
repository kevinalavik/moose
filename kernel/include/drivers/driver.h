#ifndef DRIVERS_DRIVER_H
#define DRIVERS_DRIVER_H

typedef void (*driver_init_fn)(void);

typedef struct {
	const char *name;
	driver_init_fn init;
} driver_init_entry_t;

#define DRIVER_INIT(name, fn)                                                                      \
	static driver_init_entry_t __driver_entry_##fn                                             \
	    __attribute__((used, section(".driver_init"))) = {name, fn}

void drivers_init(void);

#endif // DRIVERS_DRIVER_H
