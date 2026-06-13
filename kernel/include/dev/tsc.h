#ifndef DEV_TSC_H
#define DEV_TSC_H

#include <stdint.h>

extern uint64_t tsc_hz;

void tsc_calibrate(void);
uint64_t tsc_to_ns(uint64_t tsc);
uint64_t tsc_read(void);

#endif // DEV_TSC_H