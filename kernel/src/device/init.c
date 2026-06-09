#include <device/device.h>
#include <sys/klog.h>

extern driver_initcall_t __driver_init_start[];
extern driver_initcall_t __driver_init_end[];

int driver_init_all(void)
{
	int ok = 0;
	for (driver_initcall_t *init = __driver_init_start;
	     init < __driver_init_end; init++) {
		if (*init && (*init)() == 0)
			ok++;
	}
	klog("driver", "initialized %d (builtin) drivers", ok);
	return ok;
}
