#include <util/printf.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

extern void putc(char);

typedef enum
{
    PRINT_LEN_NONE,
    PRINT_LEN_HH,
    PRINT_LEN_H,
    PRINT_LEN_L,
    PRINT_LEN_LL,
    PRINT_LEN_Z,
    PRINT_LEN_J,
} print_len_t;

typedef struct
{
    int width;
    int precision;
    bool left;
    bool zero;
    bool precision_set;
} print_fmt_t;

typedef struct
{
    char *buf;
    size_t size;
    size_t idx;
} snbuf_t;

typedef struct fmt_ctx fmt_ctx_t;
typedef int (*fmt_handler)(fmt_ctx_t *);

struct fmt_ctx
{
    const char *fmt;
    va_list args;
    bool is_sn;
    snbuf_t *sn;
    print_fmt_t f;
    print_len_t len;
    int count;
};

static void sn_putc(char c, snbuf_t *b)
{
    if (!b || !b->buf || b->idx + 1 >= b->size)
        return;
    b->buf[b->idx++] = c;
}

static inline void outc(char c, bool is_sn, snbuf_t *b)
{
    if (is_sn)
        sn_putc(c, b);
    else
        putc(c);
}

static int str_len(const char *s)
{
    int i = 0;
    while (s && s[i])
        i++;
    return i;
}

static void pad(int n, char c, bool is_sn, snbuf_t *b)
{
    for (int i = 0; i < n; i++)
        outc(c, is_sn, b);
}

static int print_unsigned(uintmax_t v, int base, bool upper, print_fmt_t *f, bool is_sn, snbuf_t *b)
{
    char buf[65];
    int i = 0;

    if (v == 0)
        buf[i++] = '0';
    else
    {
        while (v)
        {
            uintmax_t r = v % base;
            buf[i++] = (r < 10)
                           ? ('0' + r)
                           : ((upper ? 'A' : 'a') + (r - 10));
            v /= base;
        }
    }

    int digits = i;

    int prec = 0;
    if (f && f->precision_set)
    {
        prec = (f->precision > digits) ? (f->precision - digits) : 0;
    }

    int total = digits + prec;

    int padw = (f && f->width > total) ? (f->width - total) : 0;

    char padc = ' ';
    if (f && f->zero && !f->left && !f->precision_set)
        padc = '0';

    if (f && !f->left)
        pad(padw, padc, is_sn, b);

    pad(prec, '0', is_sn, b);

    while (i--)
        outc(buf[i], is_sn, b);

    if (f && f->left)
        pad(padw, ' ', is_sn, b);

    return digits + prec + padw;
}

static int print_signed(intmax_t v, print_fmt_t *f, bool is_sn, snbuf_t *b)
{
    int neg = (v < 0);
    uintmax_t u = neg ? (uintmax_t)(-v) : (uintmax_t)v;

    char buf[65];
    int i = 0;

    if (u == 0)
        buf[i++] = '0';
    else
    {
        while (u)
        {
            buf[i++] = '0' + (u % 10);
            u /= 10;
        }
    }

    int digits = i;
    int prec = 0;
    if (f && f->precision_set)
    {
        prec = (f->precision > digits) ? (f->precision - digits) : 0;
        f->zero = false;
    }

    int total = digits + prec + neg;
    int padw = (f && f->width > total) ? (f->width - total) : 0;
    if (f && !f->left)
        pad(padw, ' ', is_sn, b);

    if (neg)
        outc('-', is_sn, b);

    pad(prec, '0', is_sn, b);
    while (i--)
        outc(buf[i], is_sn, b);

    if (f && f->left)
        pad(padw, ' ', is_sn, b);

    return digits + prec + padw + neg;
}

static int handle_pct(fmt_ctx_t *c)
{
    outc('%', c->is_sn, c->sn);
    c->count++;
    return 1;
}

static int handle_char(fmt_ctx_t *c)
{
    char ch = va_arg(c->args, int);
    outc(ch, c->is_sn, c->sn);
    c->count++;
    return 1;
}

static int handle_str(fmt_ctx_t *c)
{
    char *s = va_arg(c->args, char *);
    if (!s)
        s = "(null)";

    int l = str_len(s);
    if (c->f.precision_set && c->f.precision < l)
        l = c->f.precision;

    int padw = (c->f.width > l) ? (c->f.width - l) : 0;
    if (!c->f.left)
        pad(padw, ' ', c->is_sn, c->sn);

    for (int i = 0; i < l; i++)
        outc(s[i], c->is_sn, c->sn);

    if (c->f.left)
        pad(padw, ' ', c->is_sn, c->sn);

    c->count += l + padw;
    return 1;
}

static int handle_int(fmt_ctx_t *c)
{
    intmax_t v = va_arg(c->args, int);
    c->count += print_signed(v, &c->f, c->is_sn, c->sn);
    return 1;
}

static int handle_uint(fmt_ctx_t *c)
{
    uintmax_t v = va_arg(c->args, uintmax_t);
    c->count += print_unsigned(v, 10, false, &c->f, c->is_sn, c->sn);
    return 1;
}

static int handle_hex(fmt_ctx_t *c)
{
    uintmax_t v = va_arg(c->args, uintmax_t);
    c->count += print_unsigned(v, 16, false, &c->f, c->is_sn, c->sn);
    return 1;
}

static int handle_HEX(fmt_ctx_t *c)
{
    uintmax_t v = va_arg(c->args, uintmax_t);
    c->count += print_unsigned(v, 16, true, &c->f, c->is_sn, c->sn);
    return 1;
}

static int handle_ptr(fmt_ctx_t *c)
{
    uintptr_t p = (uintptr_t)va_arg(c->args, void *);
    outc('0', c->is_sn, c->sn);
    outc('x', c->is_sn, c->sn);
    c->count += 2;
    print_fmt_t tmp = c->f;
    tmp.width = (tmp.width > 2) ? tmp.width - 2 : 0;
    c->count += print_unsigned(p, 16, false, &tmp, c->is_sn, c->sn);
    return 1;
}

static int handle_default(fmt_ctx_t *c)
{
    outc('%', c->is_sn, c->sn);
    outc(*c->fmt, c->is_sn, c->sn);
    c->count += 2;
    return 1;
}

static fmt_handler table[256];

static void init_table(void)
{
    for (int i = 0; i < 256; i++)
        table[i] = handle_default;

    table['%'] = handle_pct;
    table['c'] = handle_char;
    table['s'] = handle_str;
    table['d'] = handle_int;
    table['i'] = handle_int;
    table['u'] = handle_uint;
    table['x'] = handle_hex;
    table['X'] = handle_HEX;
    table['p'] = handle_ptr;
}

static void parse_flags(fmt_ctx_t *c)
{
    c->f = (print_fmt_t){0, 0, false, false, false};

    while (*c->fmt)
    {
        if (*c->fmt == '-')
        {
            c->f.left = true;
            c->fmt++;
            continue;
        }
        if (*c->fmt == '0')
        {
            c->f.zero = true;
            c->fmt++;
            continue;
        }
        break;
    }

    while (*c->fmt >= '0' && *c->fmt <= '9')
    {
        c->f.width = c->f.width * 10 + (*c->fmt - '0');
        c->fmt++;
    }

    if (*c->fmt == '.')
    {
        c->fmt++;
        c->f.precision_set = true;

        while (*c->fmt >= '0' && *c->fmt <= '9')
        {
            c->f.precision = c->f.precision * 10 + (*c->fmt - '0');
            c->fmt++;
        }
    }
}

static void parse_len(fmt_ctx_t *c)
{
    if (*c->fmt == 'h')
    {
        c->fmt++;
        c->len = (*c->fmt == 'h') ? (++c->fmt, PRINT_LEN_HH) : PRINT_LEN_H;
    }
    else if (*c->fmt == 'l')
    {
        c->fmt++;
        c->len = (*c->fmt == 'l') ? (++c->fmt, PRINT_LEN_LL) : PRINT_LEN_L;
    }
    else if (*c->fmt == 'z')
    {
        c->len = PRINT_LEN_Z;
        c->fmt++;
    }
    else if (*c->fmt == 'j')
    {
        c->len = PRINT_LEN_J;
        c->fmt++;
    }
}

static int vcore(fmt_ctx_t *c)
{
    if (!table['%'])
        init_table();

    while (*c->fmt)
    {
        if (*c->fmt != '%')
        {
            outc(*c->fmt++, c->is_sn, c->sn);
            c->count++;
            continue;
        }

        c->fmt++;
        if (!*c->fmt)
            break;

        parse_flags(c);
        parse_len(c);

        fmt_handler h = table[(unsigned char)*c->fmt];
        h(c);

        c->fmt++;
    }

    return c->count;
}

int PRINTF_PREFIX(vprintf)(const char *fmt, va_list args)
{
    fmt_ctx_t c;
    c.fmt = fmt;
    c.is_sn = false;
    c.sn = NULL;
    c.count = 0;
    c.len = PRINT_LEN_NONE;
    c.f = (print_fmt_t){0};
    va_copy(c.args, args);
    int r = vcore(&c);
    va_end(c.args);
    return r;
}

int PRINTF_PREFIX(printf)(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int r = PRINTF_PREFIX(vprintf)(fmt, args);
    va_end(args);
    return r;
}

int PRINTF_PREFIX(vsnprintf)(char *buf, size_t size, const char *fmt, va_list args)
{
    snbuf_t sn = {buf, size, 0};
    fmt_ctx_t c;
    c.fmt = fmt;
    c.is_sn = true;
    c.sn = &sn;
    c.count = 0;
    c.len = PRINT_LEN_NONE;
    c.f = (print_fmt_t){0};
    va_copy(c.args, args);
    int r = vcore(&c);
    va_end(c.args);
    if (size)
    {
        if (sn.idx < size)
            buf[sn.idx] = '\0';
        else
            buf[size - 1] = '\0';
    }
    return r;
}

int PRINTF_PREFIX(snprintf)(char *buf, size_t size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int r = PRINTF_PREFIX(vsnprintf)(buf, size, fmt, args);
    va_end(args);
    return r;
}