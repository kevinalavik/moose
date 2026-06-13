#include <lib/printk.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

extern void putc(char);

static const char *_parse_width(const char *fmt, int *width, int *zero)
{
	*width = 0;
	*zero = 0;

	if (*fmt == '0') {
		*zero = 1;
		fmt++;
	}

	while (*fmt >= '0' && *fmt <= '9') {
		*width = (*width * 10) + (*fmt - '0');
		fmt++;
	}

	return fmt;
}

static const char *_parse_precision(const char *fmt, int *prec, bool *has_prec, bool *prec_star)
{
	*has_prec = false;
	*prec = -1;
	*prec_star = false;

	if (*fmt != '.')
		return fmt;

	*has_prec = true;
	fmt++;

	if (*fmt == '*') {
		*prec_star = true;
		fmt++;
		return fmt;
	}

	*prec = 0;
	while (*fmt >= '0' && *fmt <= '9') {
		*prec = (*prec * 10) + (*fmt - '0');
		fmt++;
	}

	return fmt;
}

static char *_buf;
static size_t _buf_len;
static size_t _buf_i;

static void _putc(char c)
{
	if (!_buf) {
		putc(c);
		return;
	}

	if (_buf_i < _buf_len - 1)
		_buf[_buf_i] = c;

	_buf_i++;
}

void _printstr(const char *v, int prec, bool has_prec)
{
	if (!has_prec) {
		while (*v)
			_putc(*v++);
	} else {
		for (int i = 0; i < prec && *v; i++)
			_putc(*v++);
	}
}

void _printnumber(unsigned long base, bool sig, int64_t v, unsigned long padding)
{
	unsigned long digits = 1;
	int64_t tmp = v;
	int neg = 0;

	if (base == 10 && sig && v < 0) {
		neg = 1;
		v = -v;
		tmp = v;
	}

	if (base == 10) {
		while (tmp >= 10) {
			tmp /= 10;
			digits++;
		}
	} else if (base == 16) {
		uint64_t utmp = (uint64_t)v;
		digits = 1;
		while (utmp >= 16) {
			utmp /= 16;
			digits++;
		}
	}

	while (padding > digits + (unsigned long)neg) {
		_putc('0');
		padding--;
	}

	if (neg)
		_putc('-');

	if (base == 10) {
		if (v >= 10)
			_printnumber(base, false, v / 10, 0);
		_putc((char)((v % 10) + '0'));
	}

	if (base == 16) {
		uint64_t uv = (uint64_t)v;
		if (uv >= 16)
			_printnumber(base, false, (int64_t)(uv / 16), 0);

		unsigned int x = uv % 16;
		_putc(x < 10 ? (char)(x + '0') : (char)(x - 10 + 'a'));
	}
}

/* all supported len modifiers for now */
typedef enum {
	LEN_DEFAULT,
	LEN_LONG,
	LEN_LONGLONG,
} len_mod_t;

static const char *_parse_len(const char *fmt, len_mod_t *len)
{
	*len = LEN_DEFAULT;
	if (*fmt == 'l') {
		fmt++;
		if (*fmt == 'l') {
			*len = LEN_LONGLONG;
			fmt++;
		} else {
			*len = LEN_LONG;
		}
	}
	return fmt;
}

static void _vprint(const char *fmt, va_list list)
{
	while (*fmt) {
		if (*fmt != '%') {
			_putc(*fmt++);
			continue;
		}

		fmt++;

		int width, zero;
		fmt = _parse_width(fmt, &width, &zero);

		int prec;
		bool has_prec, prec_star;
		fmt = _parse_precision(fmt, &prec, &has_prec, &prec_star);

		if (prec_star) {
			prec = va_arg(list, int);
			if (prec < 0) {
				has_prec = false;
				prec = -1;
			}
		}

		len_mod_t len;
		fmt = _parse_len(fmt, &len);

		switch (*fmt) {
		case 's': {
			const char *v = va_arg(list, const char *);
			if (!v)
				v = "(null)";
			_printstr(v, prec, has_prec);
			fmt++;
			break;
		}

		case 'd': {
			int64_t v;
			if (len == LEN_LONGLONG)
				v = va_arg(list, long long);
			else if (len == LEN_LONG)
				v = va_arg(list, long);
			else
				v = va_arg(list, int);
			unsigned long pad = has_prec ? (unsigned long)prec : (unsigned long)width;
			_printnumber(10, true, v, pad);
			fmt++;
			break;
		}

		case 'u': {
			uint64_t v;
			if (len == LEN_LONGLONG)
				v = va_arg(list, unsigned long long);
			else if (len == LEN_LONG)
				v = va_arg(list, unsigned long);
			else
				v = va_arg(list, unsigned int);
			unsigned long pad = has_prec ? (unsigned long)prec : (unsigned long)width;
			_printnumber(10, false, (int64_t)v, pad);
			fmt++;
			break;
		}

		case 'x': {
			uint64_t v;
			if (len == LEN_LONGLONG)
				v = va_arg(list, unsigned long long);
			else if (len == LEN_LONG)
				v = va_arg(list, unsigned long);
			else
				v = va_arg(list, unsigned int);

			unsigned long pad = has_prec ? (unsigned long)prec : (unsigned long)width;
			_printnumber(16, false, (int64_t)v, pad);
			fmt++;
			break;
		}

		case 'p': {
			uintptr_t v = (uintptr_t)va_arg(list, void *);

			_putc('0');
			_putc('x');

			int n = sizeof(uintptr_t) * 2;
			int shift = (n - 1) * 4;

			for (int i = 0; i < n; i++) {
				unsigned d = (v >> shift) & 0xF;
				_putc(d < 10 ? '0' + d : 'a' + (d - 10));
				shift -= 4;
			}

			fmt++;
			break;
		}

		case '%':
			_putc('%');
			fmt++;
			break;

		default:
			_putc('%');
			_putc(*fmt++);
			break;
		}
	}
}

void printk(const char *fmt, ...)
{
	va_list list;
	va_start(list, fmt);
	_buf = NULL;
	_buf_len = 0;
	_buf_i = 0;
	_vprint(fmt, list);
	va_end(list);
}

void vprintk(const char *fmt, va_list ap)
{
	_buf = NULL;
	_buf_len = 0;
	_buf_i = 0;
	_vprint(fmt, ap);
}

int vsnprintk(char *buf, size_t len, const char *fmt, va_list ap)
{
	_buf = buf;
	_buf_len = len;
	_buf_i = 0;

	if (len == 0)
		return 0;

	_vprint(fmt, ap);
	if (buf)
		buf[_buf_i < len ? _buf_i : len - 1] = '\0';

	return (int)_buf_i;
}

int snprintk(char *buf, size_t len, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vsnprintk(buf, len, fmt, ap);
	va_end(ap);
	return r;
}