#include <util/printf.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

extern void putc(char); /* defined in term.h*/

int _print_char(char ch)
{
    putc(ch);
    return 1;
}

int _print_str(char *s)
{
    int c = 0;

    if (s == NULL)
        s = "(null)";

    while (*s)
    {
        putc(*s++);
        c++;
    }

    return c;
}

int _print_number(int value, int base, bool is_signed)
{
    char buf[32];
    int i = 0;
    int count = 0;
    unsigned int n;

    if (base < 2 || base > 16)
        return 0;

    if (is_signed && value < 0)
    {
        putc('-');
        count++;
        n = -(value + 1) + 1;
    }
    else
    {
        n = value;
    }

    if (n == 0)
    {
        putc('0');
        return count + 1;
    }

    while (n != 0)
    {
        unsigned int rem = n % base;

        if (rem < 10)
            buf[i] = rem + '0';
        else
            buf[i] = (rem - 10) + 'a';

        i++;
        n = n / base;
    }

    while (i > 0)
    {
        i--;
        putc(buf[i]);
        count++;
    }

    return count;
}

int _print_addr(uintptr_t value, unsigned base)
{
    char buf[sizeof(uintptr_t) * 8];
    unsigned i = 0;
    int count = 0;
    unsigned min_width = sizeof(uintptr_t) * 2;

    if (base < 2 || base > 16)
        return 0;

    if (value == 0)
    {
        buf[i++] = '0';
    }
    else
    {
        while (value != 0)
        {
            uintptr_t rem = value % base;
            if (rem < 10)
                buf[i++] = '0' + rem;
            else
                buf[i++] = 'a' + (rem - 10);
            value /= base;
        }
    }

    while (i < min_width)
    {
        putc('0');
        count++;
        min_width--;
    }

    while (i > 0)
    {
        putc(buf[--i]);
        count++;
    }

    return count;
}

int PRINTF_PREFIX(vprintf)(const char *fmt, va_list vlist)
{
    (void)vlist;
    int c = 0;
    while (*fmt)
    {
        if (*fmt != '%')
        {
            putc(*fmt++);
            c++;
            continue;
        }

        fmt++; /* on % we skip it so we can parse example: %s */
        if (*fmt == '\0')
            break; /* if there is a single % on the end off the string we skip  it*/

        switch (*fmt)
        {
        case '%': /* %% -> % */
            putc('%');
            c++;
            break;
        case 'c': /* %c -> <char> */
            char ch = va_arg(vlist, int);
            putc(ch);
            c++;
            break;
        case 's': /* %s -> <string>*/
            char *s = va_arg(vlist, char *);
            c += _print_str(s);
            break;
        case 'd': /* %d -> <int> */
            int d = va_arg(vlist, int);
            c += _print_number(d, 10, true);
            break;
        case 'u': /* %d -> <uint> */
            unsigned u = va_arg(vlist, unsigned int);
            c += _print_number(u, 10, false);
            break;
        case 'x': /* %x -> <hex> */
            int x = va_arg(vlist, int);
            c += _print_number(x, 16, false);
            break;
        case 'p': /* %p -> 0x<addr>*/ /* address is padded in _print_addr */
            void *p = va_arg(vlist, void *);
            uintptr_t addr = (uintptr_t)p;

            c += _print_str("0x");
            c += _print_addr(addr, 16);
            break;
        default: /* %<unknown> -> %<unknown>*/
            c += _print_char('%');
            c += _print_char(*fmt);
            break;
        }

        fmt++;
    }
    return 0;
}

int PRINTF_PREFIX(printf)(const char *fmt, ...)
{
    va_list arg;
    int ret = 0;
    va_start(arg, fmt);
    ret = PRINTF_PREFIX(vprintf)(fmt, arg);
    va_end(arg);
    return ret;
}