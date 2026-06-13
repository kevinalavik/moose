#include <sys/panic.h>
#include <lib/printk.h>
#include <arch/cpu.h>
#include <stdarg.h>
#include <stdint.h>
#include <lib/kconsole.h>
#include <mm/vma.h>

static volatile int _panicking = 0;

static const char *_vec_name(uint64_t v)
{
	switch (v) {
	case 0:
		return "#DE divide error";
	case 1:
		return "#DB debug";
	case 2:
		return "NMI interrupt";
	case 3:
		return "#BP breakpoint";
	case 4:
		return "#OF overflow";
	case 5:
		return "#BR bound range exceeded";
	case 6:
		return "#UD invalid opcode";
	case 7:
		return "#NM device not available";
	case 8:
		return "#DF double fault";
	case 10:
		return "#TS invalid TSS";
	case 11:
		return "#NP segment not present";
	case 12:
		return "#SS stack fault";
	case 13:
		return "#GP general protection fault";
	case 14:
		return "#PF page fault";
	case 16:
		return "#MF x87 floating point exception";
	case 17:
		return "#AC alignment check";
	case 18:
		return "#MC machine check";
	case 19:
		return "#XM SIMD exception";
	case 20:
		return "#VE virtualization exception";
	case 21:
		return "#CP control protection exception";
	case 28:
		return "#HV hypervisor injection exception";
	case 29:
		return "#VC vmm communication exception";
	case 30:
		return "#SX security exception";
	default:
		return "external interrupt / IRQ";
	}
}

void panic(int_frame_t *frame, const char *fmt, ...)
{
	if (_panicking)
		hcf();
	_panicking = 1;
	cli();

	if (frame && frame->vector == 14 && kernel_vctx) {
		uint32_t access = VMA_READ;
		if (frame->error_code & 2)
			access = VMA_WRITE;
		if (frame->error_code & 0x10)
			access |= VMA_EXEC;

		/* todo do like current_proc()->vctx instead of kernel vctx */
		if (vfault(kernel_vctx, read_cr2(), access) == 0) {
			_panicking = 0;
			return;
		}
	}

	printk(PRINTK_NOTIME "\n");
	kconsole_set_fg(0x00FF0000); // pure red

	printk(
	    PRINTK_NOTIME
	    "==================================================================================\n");
	printk(PRINTK_NOTIME "\t\t\t           @@@@@@@@@@@@@@@@@@\n"
	                     "\t\t\t         @@@@@@@@@@@@@@@@@@@@@@@\n"
	                     "\t\t\t       @@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
	                     "\t\t\t      @@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
	                     "\t\t\t     @@@@@@@@@@@@@@@/      \\@@@/   @\n"
	                     "\t\t\t    @@@@@@@@@@@@@@@@\\      @@  @___@\n"
	                     "\t\t\t    @@@@@@@@@@@@@ @@@@@@@@@@  | \\@@@@@\n"
	                     "\t\t\t    @@@@@@@@@@@@@ @@@@@@@@@\\__@_/@@@@@\n"
	                     "\t\t\t     @@@@@@@@@@@@@@@/,/,/./'/_|.\\'\\,\\\n"
	                     "\t\t\t       @@@@@@@@@@@@@|  | | | | | | | |\n"
	                     "\t\t\t                     \\_|_|_|_|_|_|_|_|\n\n");
	printk(PRINTK_NOTIME "\t--- kernel panic (");
	if (fmt) {
		va_list ap;
		va_start(ap, fmt);
		vprintk(fmt, ap);
		va_end(ap);
	}
	printk(PRINTK_NOTIME ") (moose-kernel v%d.%d.%d%s)  ---\n\t\t",
	       VER_MAJOR,
	       VER_MINOR,
	       VER_PATCH,
	       VER_NOTE);

	if (frame) {
		printk(PRINTK_NOTIME "         vector=%llu (%s) error=%llx\n",
		       frame->vector,
		       _vec_name(frame->vector),
		       frame->error_code);
		printk(PRINTK_NOTIME "\trip=%.16llx  rsp=%.16llx rflags=%.16llx\n",
		       frame->rip,
		       frame->rsp,
		       frame->rflags);
		printk(PRINTK_NOTIME "\trax=%.16llx  rbx=%.16llx rcx=%.16llx\n",
		       frame->rax,
		       frame->rbx,
		       frame->rcx);
		printk(PRINTK_NOTIME "\trdx=%.16llx\n", frame->rdx);
		printk(PRINTK_NOTIME "\trsi=%.16llx  rdi=%.16llx rbp=%.16llx\n",
		       frame->rsi,
		       frame->rdi,
		       frame->rbp);
		if (frame->vector == 14)
			printk(
			    PRINTK_NOTIME "\tcr2=%.16llx  cr3=%.16llx\n", read_cr2(), read_cr3());
	}

	printk(PRINTK_NOTIME "\n\t\t\t\t\t\t* system halted *\n");
	printk(
	    PRINTK_NOTIME
	    "==================================================================================");
	kconsole_set_fg(KCONSOLE_DEFAULT_FG);
	printk(PRINTK_NOTIME "\n");
	hcf();
	__builtin_unreachable();
}