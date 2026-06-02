#include <util/printf.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

extern void putc(char); /* defined in term.h*/

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

int _print_unsigned_number(uintmax_t value, int base, bool alternate)
{
    char buf[sizeof(uintmax_t) * 8];
    int i = 0;
    int count = 0;

    if (base < 2 || base > 16)
        return 0;

    if (alternate && base == 16 && value != 0)
    {
        count += _print_str("0x");
    }

    if (value == 0)
    {
        putc('0');
        return count + 1;
    }

    while (value != 0)
    {
        uintmax_t rem = value % base;

        if (rem < 10)
            buf[i] = rem + '0';
        else
            buf[i] = (rem - 10) + 'a';

        i++;
        value = value / base;
    }

    while (i > 0)
    {
        i--;
        putc(buf[i]);
        count++;
    }

    return count;
}

int _print_signed_number(intmax_t value, int base)
{
    int count = 0;
    uintmax_t n;

    if (base < 2 || base > 16)
        return 0;

    if (value < 0)
    {
        putc('-');
        count++;
        n = -(value + 1) + 1;
    }
    else
    {
        n = value;
    }

    return count + _print_unsigned_number(n, base, false);
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
            break; /* if there is a single % on the end off the string we skip it */

        bool alternate = false;
        print_len_t len = PRINT_LEN_NONE;

        while (*fmt == '#')
        {
            alternate = true;
            fmt++;
        }

        if (*fmt == 'h')
        {
            fmt++;
            if (*fmt == 'h')
            {
                len = PRINT_LEN_HH;
                fmt++;
            }
            else
            {
                len = PRINT_LEN_H;
            }
        }
        else if (*fmt == 'l')
        {
            fmt++;
            if (*fmt == 'l')
            {
                len = PRINT_LEN_LL;
                fmt++;
            }
            else
            {
                len = PRINT_LEN_L;
            }
        }
        else if (*fmt == 'z')
        {
            len = PRINT_LEN_Z;
            fmt++;
        }
        else if (*fmt == 'j')
        {
            len = PRINT_LEN_J;
            fmt++;
        }

        switch (*fmt)
        {
        case '%': /* %% -> % */
            putc('%');
            c++;
            break;

        case 'c': /* %c -> <char> */
        {
            char ch = va_arg(vlist, int);
            putc(ch);
            c++;
            break;
        }

        case 's': /* %s -> <string>*/
        {
            char *s = va_arg(vlist, char *);
            c += _print_str(s);
            break;
        }

        case 'd': /* %d -> <int> */
        case 'i': /* %i -> <int> */
        {
            intmax_t d;

            switch (len)
            {
            case PRINT_LEN_HH:
                d = (signed char)va_arg(vlist, int);
                break;
            case PRINT_LEN_H:
                d = (short)va_arg(vlist, int);
                break;
            case PRINT_LEN_L:
                d = va_arg(vlist, long);
                break;
            case PRINT_LEN_LL:
                d = va_arg(vlist, long long);
                break;
            case PRINT_LEN_Z:
                d = va_arg(vlist, long);
                break;
            case PRINT_LEN_J:
                d = va_arg(vlist, intmax_t);
                break;
            default:
                d = va_arg(vlist, int);
                break;
            }

            c += _print_signed_number(d, 10);
            break;
        }

        case 'u': /* %u -> <uint> */
        {
            uintmax_t u;

            switch (len)
            {
            case PRINT_LEN_HH:
                u = (unsigned char)va_arg(vlist, unsigned int);
                break;
            case PRINT_LEN_H:
                u = (unsigned short)va_arg(vlist, unsigned int);
                break;
            case PRINT_LEN_L:
                u = va_arg(vlist, unsigned long);
                break;
            case PRINT_LEN_LL:
                u = va_arg(vlist, unsigned long long);
                break;
            case PRINT_LEN_Z:
                u = va_arg(vlist, size_t);
                break;
            case PRINT_LEN_J:
                u = va_arg(vlist, uintmax_t);
                break;
            default:
                u = va_arg(vlist, unsigned int);
                break;
            }

            c += _print_unsigned_number(u, 10, false);
            break;
        }

        case 'x': /* %x -> <hex> */
        {
            uintmax_t x;

            switch (len)
            {
            case PRINT_LEN_HH:
                x = (unsigned char)va_arg(vlist, unsigned int);
                break;
            case PRINT_LEN_H:
                x = (unsigned short)va_arg(vlist, unsigned int);
                break;
            case PRINT_LEN_L:
                x = va_arg(vlist, unsigned long);
                break;
            case PRINT_LEN_LL:
                x = va_arg(vlist, unsigned long long);
                break;
            case PRINT_LEN_Z:
                x = va_arg(vlist, size_t);
                break;
            case PRINT_LEN_J:
                x = va_arg(vlist, uintmax_t);
                break;
            default:
                x = va_arg(vlist, unsigned int);
                break;
            }

            c += _print_unsigned_number(x, 16, alternate);
            break;
        }

        case 'p': /* %p -> 0x<addr>*/ /* address is padded in _print_addr */
        {
            void *p = va_arg(vlist, void *);
            uintptr_t addr = (uintptr_t)p;

            c += _print_str("0x");
            c += _print_addr(addr, 16);
            break;
        }

        default: /* %<unknown> -> %<unknown>*/
            c += _print_char('%');

            if (alternate)
                c += _print_char('#');

            switch (len)
            {
            case PRINT_LEN_HH:
                c += _print_char('h');
                c += _print_char('h');
                break;
            case PRINT_LEN_H:
                c += _print_char('h');
                break;
            case PRINT_LEN_L:
                c += _print_char('l');
                break;
            case PRINT_LEN_LL:
                c += _print_char('l');
                c += _print_char('l');
                break;
            case PRINT_LEN_Z:
                c += _print_char('z');
                break;
            case PRINT_LEN_J:
                c += _print_char('j');
                break;
            default:
                break;
            }

            c += _print_char(*fmt);
            break;
        }

        fmt++;
    }

    return c;
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