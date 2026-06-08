#ifndef DEV_TSC_H
#define DEV_TSC_H

#include <stdint.h>

void tsc_init(void);
uint64_t tsc_uptime_ms(void);
uint64_t tsc_uptime_us(void);
uint64_t tsc_get_khz(void);

#endif /* DEV_TSC_H */
