#include <dev/tsc.h>
#include <dev/dev.h>
#include <arch/cpu.h>
#include <lib/string.h>
#include <sys/klog.h>
#include <stdint.h>
#include <stddef.h>

/* thanks to:
    - https://wiki.osdev.org/Programmable_Interval_Timer
    - https://wiki.osdev.org/TSC
    - https://elixir.bootlin.com/linux/v7.0.11/source/arch/x86/kernel/tsc.c

Most of this code has been ported from linux.
*/

#define PIT_CH0 0x40
#define PIT_CH2 0x42
#define PIT_CMD 0x43
#define PIT_GATE 0x61
#define PIT_TICK_RATE 1193182UL

#define CAL_MS 10UL
#define CAL_LATCH (PIT_TICK_RATE / (1000UL / CAL_MS))
#define CAL_PIT_LOOPS 1000

#define CAL2_MS 50UL
#define CAL2_LATCH (PIT_TICK_RATE / (1000UL / CAL2_MS))
#define CAL2_PIT_LOOPS 5000

#define MAX_QUICK_PIT_MS 50
#define MAX_QUICK_PIT_ITERATIONS (MAX_QUICK_PIT_MS * PIT_TICK_RATE / 1000 / 256)

static uint64_t tsc_boot = 0;
static uint64_t tsc_khz = 0;
static int tsc_ready = 0;

static void pit_ch2_gate_on(void)
{
	outb((inb(PIT_GATE) & ~0x02) | 0x01, PIT_GATE);
}

static void pit_ch2_arm(uint16_t latch)
{
	outb(0xb0, PIT_CMD);
	outb((uint8_t)(latch & 0xff), PIT_CH2);
	outb((uint8_t)(latch >> 8), PIT_CH2);
}

static inline uint8_t pit_ch2_msb(void)
{
	inb(PIT_CH2);
	return inb(PIT_CH2);
}

static int pit_expect_msb(uint8_t val, uint64_t *tscp, uint64_t *deltap)
{
	int count;
	uint64_t tsc = 0, prev_tsc = 0;

	for (count = 0; count < 50000; count++) {
		if (pit_ch2_msb() != val)
			break;
		prev_tsc = tsc;
		tsc = rdtsc();
	}

	*deltap = rdtsc() - prev_tsc;
	*tscp = tsc;
	return count > 5;
}

static inline int pit_verify_msb(uint8_t val)
{
	inb(PIT_CH2);
	return inb(PIT_CH2) == val;
}

static uint64_t quick_pit_calibrate(void)
{
	uint64_t i, tsc, delta, d1, d2;

	pit_ch2_gate_on();
	outb(0xb0, PIT_CMD);
	outb(0xff, PIT_CH2);
	outb(0xff, PIT_CH2);

	pit_verify_msb(0);

	if (!pit_expect_msb(0xff, &tsc, &d1))
		return 0;

	for (i = 1; i <= MAX_QUICK_PIT_ITERATIONS; i++) {
		if (!pit_expect_msb((uint8_t)(0xff - i), &delta, &d2))
			break;

		delta -= tsc;

		if (i == 1 &&
		    d1 + d2 >= (delta * MAX_QUICK_PIT_ITERATIONS) >> 11)
			return 0;

		if (d1 + d2 >= (delta >> 11))
			continue;

		if (!pit_verify_msb((uint8_t)(0xfe - i)))
			break;

		delta *= PIT_TICK_RATE;
		delta /= i * 256 * 1000;
		klog("tsc", "fast calibration (ch2): %llu kHz (%llu MHz)",
		     delta, delta / 1000);
		return delta;
	}

	return 0;
}

static uint64_t pit_calibrate_tsc(uint16_t latch, uint64_t ms, int loopmin)
{
	uint64_t tsc, t1, t2, delta, tscmin, tscmax;
	int pitcnt;

	pit_ch2_gate_on();
	pit_ch2_arm(latch);

	tsc = t1 = t2 = rdtsc();
	pitcnt = 0;
	tscmax = 0;
	tscmin = UINT64_MAX;

	while ((inb(PIT_GATE) & 0x20) == 0) {
		t2 = rdtsc();
		delta = t2 - tsc;
		tsc = t2;
		if (delta < tscmin)
			tscmin = delta;
		if (delta > tscmax)
			tscmax = delta;
		pitcnt++;
	}

	if (pitcnt < loopmin || tscmin > UINT64_MAX / 10 ||
	    tscmax > 10 * tscmin)
		return UINT64_MAX;

	delta = t2 - t1;
	return delta / ms;
}

static uint16_t pit_ch0_read_counter(void)
{
	outb(0x00, PIT_CMD);
	uint8_t lo = inb(PIT_CH0);
	uint8_t hi = inb(PIT_CH0);
	return (uint16_t)((hi << 8) | lo);
}

static uint64_t pit_ch0_calibrate_tsc(uint64_t ms)
{
	uint16_t latch = (uint16_t)(PIT_TICK_RATE * ms / 1000);

	outb(0x34, PIT_CMD);
	outb((uint8_t)(latch & 0xff), PIT_CH0);
	outb((uint8_t)(latch >> 8), PIT_CH0);

	uint64_t deadline = rdtsc() + 200000000ULL;
	uint16_t threshold = (uint16_t)(latch * 3 / 4);
	while (pit_ch0_read_counter() > threshold) {
		if (rdtsc() > deadline)
			return 0;
	}

	while (pit_ch0_read_counter() > threshold) {
		if (rdtsc() > deadline)
			return 0;
	}

	uint64_t t0 = rdtsc();
	while (pit_ch0_read_counter() < threshold) {
		if (rdtsc() > deadline)
			return 0;
	}
	uint64_t t1 = rdtsc();

	uint64_t pit_ticks = (uint64_t)latch * 3 / 2;
	uint64_t tsc_delta = t1 - t0;
	uint64_t khz = (tsc_delta * PIT_TICK_RATE) / (pit_ticks * 1000);
	if (khz == 0)
		return 0;

	klog("tsc", "ch0 fallback calibration: %llu kHz (%llu MHz)", khz,
	     khz / 1000);
	return khz;
}

static uint64_t tsc_do_calibrate(void)
{
	uint64_t fast = quick_pit_calibrate();
	if (fast)
		return fast;

	klog("tsc", "fast calibration failed, falling back to slow path");

	uint16_t latch = (uint16_t)CAL_LATCH;
	uint64_t ms = CAL_MS;
	int loopmin = CAL_PIT_LOOPS;
	uint64_t best = UINT64_MAX;

	for (int i = 0; i < 3; i++) {
		uint64_t v = pit_calibrate_tsc(latch, ms, loopmin);
		if (v < best)
			best = v;

		if (i == 1 && best == UINT64_MAX) {
			klog("tsc", "10ms window noisy, widening to 50ms");
			latch = (uint16_t)CAL2_LATCH;
			ms = CAL2_MS;
			loopmin = CAL2_PIT_LOOPS;
		}
	}

	if (best != UINT64_MAX && best != 0) {
		klog("tsc", "slow calibration (ch2): %llu kHz (%llu MHz)", best,
		     best / 1000);
		return best;
	}

	klog("tsc", "ch2 gate unavailable, trying ch0 counter poll");

	uint64_t ch0 = pit_ch0_calibrate_tsc(CAL_MS);
	if (ch0)
		return ch0;

	klog("tsc", "all calibration methods failed, assuming 1 GHz");
	return 1000000ULL;
}

uint64_t tsc_get_khz(void)
{
	return tsc_khz;
}

uint64_t tsc_uptime_ms(void)
{
	if (!tsc_ready)
		return 0;
	return (rdtsc() - tsc_boot) / tsc_khz;
}

uint64_t tsc_uptime_us(void)
{
	if (!tsc_ready)
		return 0;
	return ((rdtsc() - tsc_boot) * 1000ULL) / tsc_khz;
}

static size_t tsc_read(handle_t *h, void *buf, size_t len)
{
	(void)h;
	if (!buf || len < sizeof(uint64_t))
		return 0;
	uint64_t ms = tsc_uptime_ms();
	memcpy(buf, &ms, sizeof(ms));
	return sizeof(uint64_t);
}

static const dev_ops_t tsc_ops = {
	.read = tsc_read,
	.write = NULL,
};

static handle_t tsc_handle = HANDLE_INVALID;

handle_t tsc_init(void)
{
	tsc_boot = rdtsc();
	tsc_khz = tsc_do_calibrate();
	tsc_ready = 1;
	tsc_handle = device_handle_make((void *)&tsc_handle, &tsc_ops, "tsc0");
	return tsc_handle;
}