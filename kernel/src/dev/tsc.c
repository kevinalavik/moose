#include <dev/tsc.h>
#include <arch/cpu.h>
#include <arch/io.h>
#include <arch/cpuid.h>
#include <lib/printk.h>
#include <stdint.h>
#include <stdbool.h>

#define KHZ 1000ULL
#define PIT_CH2 0x42
#define PIT_CMD 0x43
#define PIT_GATE 0x61
#define PIT_TICK_RATE 1193182UL

/* "KVM_CPUID_FREQ" - implemented by KVM and VMware. EAX = TSC kHz,
 * EBX = bus/APIC timer kHz. Only valid if CPUID.1:ECX[31] (hypervisor
 * present) is set and the hypervisor leaf range covers it. */
#define HYPERVISOR_CPUID_BASE 0x40000000
#define HYPERVISOR_CPUID_FREQ 0x40000010

#define MSR_PLATFORM_INFO 0x000000CE
#define MSR_AMD_PSTATE_DEF0 0xC0010064

uint64_t tsc_hz;

struct cyc2ns {
	uint64_t offset;
	uint32_t mul;
	uint32_t shift;
};

static struct cyc2ns cyc2ns;

static void set_cyc2ns_scale(uint64_t khz)
{
	uint32_t mul, shift, s;
	uint64_t tsc_now = rdtsc();
	uint64_t ns_now = tsc_now * 1000000000ULL / (khz * KHZ);

	for (s = 32; s > 0; s--) {
		uint64_t tmp = (1000000ULL << s) / khz;
		if (tmp < (1ULL << 31)) {
			mul = (uint32_t)tmp;
			shift = s;
			goto done;
		}
	}
	mul = (uint32_t)(1000000ULL / khz);
	shift = 0;
done:
	if (shift == 32) {
		shift = 31;
		mul >>= 1;
	}

	cyc2ns.offset = ns_now - ((tsc_now * mul) >> shift);
	cyc2ns.mul = mul;
	cyc2ns.shift = shift;
}

enum cpu_vendor {
	VENDOR_UNKNOWN = 0,
	VENDOR_INTEL,
	VENDOR_AMD,
};

static enum cpu_vendor get_vendor(void)
{
	uint32_t eax, ebx, ecx, edx;

	cpuid(CPUID_VENDOR, 0, &eax, &ebx, &ecx, &edx);

	/* "GenuineIntel" */
	if (ebx == 0x756e6547 && edx == 0x49656e69 && ecx == 0x6c65746e)
		return VENDOR_INTEL;

	/* "AuthenticAMD" */
	if (ebx == 0x68747541 && edx == 0x69746e65 && ecx == 0x444d4163)
		return VENDOR_AMD;

	return VENDOR_UNKNOWN;
}

static uint32_t get_family(void)
{
	uint32_t eax, ebx, ecx, edx;
	uint32_t base_family, ext_family;

	cpuid(CPUID_FEATURES, 0, &eax, &ebx, &ecx, &edx);

	base_family = (eax >> 8) & 0xF;
	ext_family = (eax >> 20) & 0xFF;

	return (base_family == 0xF) ? base_family + ext_family : base_family;
}

/*
 * Ask the hypervisor directly for the TSC frequency. KVM and VMware both
 * implement CPUID leaf 0x40000010 ("KVM_CPUID_FREQ") which returns:
 *   EAX = TSC frequency in kHz
 *   EBX = bus/local-APIC timer frequency in kHz
 *
 * This is exact (no measurement needed) and it doesnt depend on cpu vendor it should work
 * regardless if the host CPU is Intel or AMD.
 */
static uint64_t hypervisor_tsc_khz(void)
{
	uint32_t eax, ebx, ecx, edx;

	cpuid(CPUID_FEATURES, 0, &eax, &ebx, &ecx, &edx);
	if (!(ecx & (1U << 31))) /* hypervisor present bit */
		return 0;

	cpuid(HYPERVISOR_CPUID_BASE, 0, &eax, &ebx, &ecx, &edx);
	if (eax < HYPERVISOR_CPUID_FREQ)
		return 0;

	cpuid(HYPERVISOR_CPUID_FREQ, 0, &eax, &ebx, &ecx, &edx);
	if (eax < 100000 || eax > 10000000)
		return 0;

	return eax;
}

/*
 * CPUID leaf 0x15 ("TSC/Core Crystal Clock Information"):
 *   EAX = denominator, EBX = numerator of the TSC/crystal ratio
 *   ECX = crystal clock frequency in Hz (0 if not reported)
 *
 * TSC frequency = crystal_hz * EBX / EAX.
 *
 * Supported on Intel Skylake+ and AMD Zen+ (iirc). When the crystal frequency
 * isn not reported (ECX == 0), both Intel and AMD document fixed
 * defaults (24 MHz and 25 MHz respectively) which i just fall back to
 */
static uint64_t cpuid_tsc_calibrate(void)
{
	uint32_t max_leaf, ebx, ecx, edx;
	uint32_t denom, num, crystal_khz;

	cpuid(CPUID_VENDOR, 0, &max_leaf, &ebx, &ecx, &edx);
	if (max_leaf < 0x15)
		return 0;

	cpuid(0x15, 0, &denom, &num, &ecx, &edx);
	if (num == 0 || denom == 0)
		return 0;

	crystal_khz = ecx / KHZ;

	if (crystal_khz == 0) {
		switch (get_vendor()) {
		case VENDOR_INTEL:
			crystal_khz = 24000; /* 24.0 MHz */
			break;
		case VENDOR_AMD:
			crystal_khz = 25000; /* 25.0 MHz */
			break;
		default:
			return 0;
		}
	}

	return (uint64_t)crystal_khz * num / denom;
}

/*
 * MSR-based calibration for CPUs that don't implement CPUID leaf 0x15.
 *
 * Intel (Nehalem+, i think): MSR_PLATFORM_INFO[15:8] is the "Max Non-Turbo Ratio",
 * each unit being 100 MHz.
 *
 * AMD/Hygon (Zen, family >= 0x17): PStateDef[0] (MSRC001_0064) encodes
 * CpuFid (bits 7:0) and CpuDfsId (bits 13:8). The core frequency is
 * 200 MHz * CpuFid / CpuDfsId, valid when PstateEn (bit 63) is set.
 */
static uint64_t msr_calibrate(void)
{
	switch (get_vendor()) {
	case VENDOR_INTEL: {
		uint64_t info = rdmsr(MSR_PLATFORM_INFO);
		uint32_t ratio = (info >> 8) & 0xFF;

		if (ratio == 0)
			return 0;

		return (uint64_t)ratio * 100 * KHZ;
	}
	case VENDOR_AMD: {
		uint64_t pstate;
		uint32_t fid, did;

		if (get_family() < 0x17)
			return 0;

		pstate = rdmsr(MSR_AMD_PSTATE_DEF0);
		if (!(pstate & (1ULL << 63))) /* PstateEn */
			return 0;

		fid = pstate & 0xFF;
		did = (pstate >> 8) & 0x3F;
		if (did == 0)
			return 0;

		return (200ULL * KHZ * fid) / did;
	}
	default:
		return 0;
	}
}

#define MSR_IA32_TSC_ADJUST 0x0000003B

static void tsc_sanitize_adjust(void)
{
	uint32_t max_leaf, ebx, eax, ecx, edx;

	cpuid(CPUID_VENDOR, 0, &max_leaf, &ebx, &ecx, &edx);
	if (max_leaf < CPUID_EXT_FEATURES)
		return;

	cpuid(CPUID_EXT_FEATURES, 0, &eax, &ebx, &ecx, &edx);
	if (!((ebx >> 1) & 1)) /* IA32_TSC_ADJUST support */
		return;

	uint64_t adjust = rdmsr(MSR_IA32_TSC_ADJUST);
	if (adjust)
		wrmsr(MSR_IA32_TSC_ADJUST, 0);
}

static inline int pit_verify_msb(uint8_t val)
{
	inb(PIT_CH2);
	return inb(PIT_CH2) == val;
}

static inline int pit_expect_msb(uint8_t val, uint64_t *tscp, uint64_t *deltap)
{
	int count;
	uint64_t tsc = 0, prev_tsc = 0;

	for (count = 0; count < 50000; count++) {
		if (!pit_verify_msb(val))
			break;
		prev_tsc = tsc;
		tsc = rdtsc();
	}
	*deltap = rdtsc() - prev_tsc;
	*tscp = tsc;

	return count > 5;
}

static uint64_t quick_pit_calibrate(void)
{
	int i;
	uint64_t tsc, delta, d1, d2;
	unsigned long max_iter = 50UL * PIT_TICK_RATE / 1000UL / 256UL;

	/* Gate channel 2 on, speaker off. */
	outb(PIT_GATE, (inb(PIT_GATE) & ~0x02) | 0x01);

	/* Channel 2, mode 0 (one-shot), 16-bit binary, latch 0xFFFF. */
	outb(PIT_CMD, 0xB0);
	outb(PIT_CH2, 0xFF);
	outb(PIT_CH2, 0xFF);

	if (!pit_expect_msb(0xFF, &tsc, &d1))
		return 0;

	for (i = 1; i <= (int)max_iter; i++) {
		if (!pit_expect_msb(0xFF - i, &delta, &d2))
			break;

		delta -= tsc;

		if (i == 1 && d1 + d2 >= (delta * max_iter) >> 11)
			return 0;

		if (d1 + d2 >= delta >> 11)
			continue;

		if (!pit_verify_msb(0xFE - i))
			break;

		delta *= PIT_TICK_RATE;
		delta /= i * 256UL * KHZ;
		return delta;
	}
	return 0;
}

static uint64_t pit_loop_calibrate(void)
{
	uint64_t tscmin = UINT64_MAX, tscmax = 0;
	uint64_t t1, t2, delta, tsc;
	int pitcnt;
	uint32_t latch = PIT_TICK_RATE / 100;

	/* Gate channel 2 on, speaker off. */
	outb(PIT_GATE, (inb(PIT_GATE) & ~0x02) | 0x01);

	/* Channel 2, mode 0 (one-shot), 16-bit binary, ~10ms latch. */
	outb(PIT_CMD, 0xB0);
	outb(PIT_CH2, latch & 0xFF);
	outb(PIT_CH2, latch >> 8);

	tsc = t1 = rdtsc();
	pitcnt = 0;

	while (!(inb(PIT_GATE) & 0x20)) {
		t2 = rdtsc();
		delta = t2 - tsc;
		tsc = t2;
		if (delta < tscmin)
			tscmin = delta;
		if (delta > tscmax)
			tscmax = delta;
		pitcnt++;
	}

	if (pitcnt < 1000 || tscmax > 10 * tscmin)
		return 0;

	delta = t2 - t1;
	delta /= 10;
	return delta;
}

void tsc_calibrate(void)
{
	uint64_t khz = 0;
	const char *method;

	tsc_sanitize_adjust();

	if ((khz = hypervisor_tsc_khz()) != 0) {
		method = "hypervisor CPUID";
	} else if ((khz = cpuid_tsc_calibrate()) != 0) {
		method = "CPUID leaf 0x15";
	} else if ((khz = msr_calibrate()) != 0) {
		method = "MSR";
	} else if ((khz = quick_pit_calibrate()) != 0) {
		method = "PIT (quick)";
	} else if ((khz = pit_loop_calibrate()) != 0) {
		method = "PIT (loop)";
	} else {
		method = "fallback";
	}

	if (!khz || khz < 100000 || khz >= 10000000) {
		printk("tsc: calibration failed, assuming 1 GHz\n");
		khz = 1000000ULL;
		method = "fallback";
	}

	tsc_hz = khz * KHZ;
	set_cyc2ns_scale(khz);

	printk("tsc: %llu.%03llu MHz (%llu kHz) [%s]\n", khz / KHZ, khz % KHZ, khz, method);
}

uint64_t tsc_read(void)
{
	return rdtsc_ordered();
}

uint64_t tsc_to_ns(uint64_t tsc)
{
	uint64_t ns = cyc2ns.offset;
	ns += ((uint64_t)tsc * cyc2ns.mul) >> cyc2ns.shift;
	return ns;
}