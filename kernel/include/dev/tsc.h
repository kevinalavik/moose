#ifndef DEV_TSC_H
#define DEV_TSC_H

#include <stdint.h>

typedef struct handle handle_t; // make the compiler happy
handle_t tsc_init(void);
uint64_t tsc_uptime_ms(void);
uint64_t tsc_uptime_us(void);
uint64_t tsc_get_khz(void);

#endif /* DEV_TSC_H */
