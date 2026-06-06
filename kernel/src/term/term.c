#include <term/term.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define TERM_DEFAULT_FG 7
#define TERM_DEFAULT_BG 0

#define TAB_WIDTH 8
#define CURSOR_MASK 0x00ffffff

static void term_reset(term_t *t);
static int param(term_t *t, uint32_t i, int fallback);

static const uint32_t ansi16[16] = {
    RGB(0, 0, 0),
    RGB(170, 0, 0),
    RGB(0, 170, 0),
    RGB(170, 85, 0),
    RGB(0, 0, 170),
    RGB(170, 0, 170),
    RGB(0, 170, 170),
    RGB(170, 170, 170),
    RGB(85, 85, 85),
    RGB(255, 85, 85),
    RGB(85, 255, 85),
    RGB(255, 255, 85),
    RGB(85, 85, 255),
    RGB(255, 85, 255),
    RGB(85, 255, 255),
    RGB(255, 255, 255),
};

static uint32_t rgb(uint32_t r, uint32_t g, uint32_t b)
{
    return ((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff);
}

static uint32_t cube_value(uint32_t n)
{
    return n == 0 ? 0 : 55 + n * 40;
}

static uint32_t ansi_color(uint16_t n)
{
    if (n < 16)
        return ansi16[n];

    if (n < 232)
    {
        uint32_t v = n - 16;
        return rgb(cube_value(v / 36),
                   cube_value((v / 6) % 6),
                   cube_value(v % 6));
    }

    if (n < 256)
    {
        uint32_t v = 8 + (n - 232) * 10;
        return rgb(v, v, v);
    }

    return ansi16[TERM_DEFAULT_FG];
}

static volatile uint32_t *pixels(term_t *t)
{
    return (volatile uint32_t *)t->fb->address;
}

static uint32_t pitch(term_t *t)
{
    return (uint32_t)(t->fb->pitch / sizeof(uint32_t));
}

static uint32_t cw(term_t *t) { return t->font.width; }
static uint32_t ch(term_t *t) { return t->font.height; }

static uint32_t cols(term_t *t)
{
    uint32_t w = cw(t);
    return w == 0 ? 0 : (uint32_t)t->fb->width / w;
}

static uint32_t rows(term_t *t)
{
    uint32_t h = ch(t);
    return h == 0 ? 0 : (uint32_t)t->fb->height / h;
}

static uint16_t effective_fg(term_t *t)
{
    return (t->bold && t->fg < 8) ? t->fg + 8 : t->fg;
}

static uint32_t real_fg(term_t *t)
{
    return t->fg_is_rgb ? t->fg_rgb : ansi_color(effective_fg(t));
}

static uint32_t real_bg(term_t *t)
{
    return t->bg_is_rgb ? t->bg_rgb : ansi_color(t->bg);
}

static uint32_t fg_col(term_t *t)
{
    return t->reverse ? real_bg(t) : real_fg(t);
}

static uint32_t bg_col(term_t *t)
{
    return t->reverse ? real_fg(t) : real_bg(t);
}

static void set_fg(term_t *t, uint16_t n)
{
    if (n < 256)
    {
        t->fg = n;
        t->fg_is_rgb = false;
    }
}

static void set_bg(term_t *t, uint16_t n)
{
    if (n < 256)
    {
        t->bg = n;
        t->bg_is_rgb = false;
    }
}

static void set_fg_rgb(term_t *t, uint32_t r, uint32_t g, uint32_t b)
{
    if (r <= 255 && g <= 255 && b <= 255)
    {
        t->fg_rgb = rgb(r, g, b);
        t->fg_is_rgb = true;
    }
}

static void set_bg_rgb(term_t *t, uint32_t r, uint32_t g, uint32_t b)
{
    if (r <= 255 && g <= 255 && b <= 255)
    {
        t->bg_rgb = rgb(r, g, b);
        t->bg_is_rgb = true;
    }
}

static void reset_attrs(term_t *t)
{
    t->fg = TERM_DEFAULT_FG;
    t->bg = TERM_DEFAULT_BG;
    t->fg_rgb = 0;
    t->bg_rgb = 0;
    t->fg_is_rgb = false;
    t->bg_is_rgb = false;
    t->bold = false;
    t->reverse = false;
}

static void clear_rect(term_t *t,
                       uint32_t x0, uint32_t y0,
                       uint32_t w, uint32_t h, uint32_t color)
{
    if (!t->fb || !t->fb->address)
        return;
    if (x0 >= t->fb->width || y0 >= t->fb->height)
        return;

    if (x0 + w > t->fb->width)
        w = t->fb->width - x0;
    if (y0 + h > t->fb->height)
        h = t->fb->height - y0;

    volatile uint32_t *fb = pixels(t);
    uint32_t pt = pitch(t);

    for (uint32_t y = 0; y < h; y++)
    {
        volatile uint32_t *row = fb + (y0 + y) * pt + x0;
        for (uint32_t x = 0; x < w; x++)
            row[x] = color;
    }
}

static void scroll_pixels(term_t *t, uint32_t amount)
{
    if (!t->fb || !t->fb->address || amount == 0)
        return;

    if (amount >= t->fb->height)
    {
        clear_rect(t, 0, 0, t->fb->width, t->fb->height, bg_col(t));
        return;
    }

    volatile uint32_t *fb = pixels(t);
    uint32_t pt = pitch(t);
    uint32_t h = t->fb->height - amount;

    for (uint32_t y = 0; y < h; y++)
    {
        volatile uint32_t *dst = fb + y * pt;
        volatile uint32_t *src = fb + (y + amount) * pt;
        for (uint32_t x = 0; x < t->fb->width; x++)
            dst[x] = src[x];
    }
    clear_rect(t, 0, h, t->fb->width, amount, bg_col(t));
}

static void scroll_lines(term_t *t, uint32_t n)
{
    uint32_t h = ch(t);
    if (h == 0 || n == 0)
        return;
    scroll_pixels(t, h * n);
}

static void ensure_visible(term_t *t)
{
    uint32_t h = ch(t);
    if (!t->fb || h == 0)
        return;

    if (h > t->fb->height)
    {
        t->cy = 0;
        return;
    }

    while (t->cy + h > t->fb->height)
    {
        scroll_lines(t, 1);
        t->cy = (t->cy >= h) ? t->cy - h : 0;
    }
}

static void cursor_flip(term_t *t)
{
    if (!t->fb || !t->fb->address)
        return;

    uint32_t cw_ = cw(t), ch_ = ch(t);
    if (cw_ == 0 || ch_ == 0)
        return;
    if (t->cx + cw_ > t->fb->width || t->cy + ch_ > t->fb->height)
        return;

    volatile uint32_t *fb = pixels(t);
    uint32_t pt = pitch(t);

    for (uint32_t y = 0; y < ch_; y++)
    {
        volatile uint32_t *row = fb + (t->cy + y) * pt + t->cx;
        for (uint32_t x = 0; x < cw_; x++)
            row[x] ^= CURSOR_MASK;
    }
}

static void cursor_hide(term_t *t)
{
    if (!t->cursor_drawn)
        return;
    cursor_flip(t);
    t->cursor_drawn = false;
}

static void cursor_show(term_t *t)
{
    if (!t->cursor_on || t->cursor_drawn)
        return;
    ensure_visible(t);
    cursor_flip(t);
    t->cursor_drawn = true;
}

static uint32_t col(term_t *t) { return cw(t) == 0 ? 0 : t->cx / cw(t); }
static uint32_t row(term_t *t) { return ch(t) == 0 ? 0 : t->cy / ch(t); }

static void set_cursor(term_t *t, uint32_t r, uint32_t c)
{
    uint32_t rs = rows(t), cs = cols(t);
    if (rs == 0 || cs == 0)
        return;
    if (r >= rs)
        r = rs - 1;
    if (c >= cs)
        c = cs - 1;
    t->cx = c * cw(t);
    t->cy = r * ch(t);
    t->wrap_pending = false;
}

static void save_cursor(term_t *t)
{
    t->saved_cx = t->cx;
    t->saved_cy = t->cy;
    t->saved_wrap_pending = t->wrap_pending;
}

static void restore_cursor(term_t *t)
{
    t->cx = t->saved_cx;
    t->cy = t->saved_cy;
    t->wrap_pending = t->saved_wrap_pending;
    ensure_visible(t);
}

static void cursor_up(term_t *t, uint32_t n)
{
    uint32_t r = row(t);
    set_cursor(t, n > r ? 0 : r - n, col(t));
}

static void cursor_down(term_t *t, uint32_t n)
{
    uint32_t r = row(t), rs = rows(t);
    if (rs == 0)
        return;
    r += n;
    set_cursor(t, r >= rs ? rs - 1 : r, col(t));
}

static void cursor_forward(term_t *t, uint32_t n)
{
    uint32_t c = col(t), cs = cols(t);
    if (cs == 0)
        return;
    c += n;
    set_cursor(t, row(t), c >= cs ? cs - 1 : c);
}

static void cursor_back(term_t *t, uint32_t n)
{
    uint32_t c = col(t);
    set_cursor(t, row(t), n > c ? 0 : c - n);
}

static void newline(term_t *t)
{
    uint32_t h = ch(t);
    if (h == 0)
        return;
    t->cx = 0;
    t->cy += h;
    t->wrap_pending = false;
    ensure_visible(t);
}

static void index_(term_t *t)
{
    uint32_t h = ch(t);
    if (h == 0)
        return;
    t->cy += h;
    t->wrap_pending = false;
    ensure_visible(t);
}

static void backspace(term_t *t)
{
    uint32_t w = cw(t);
    if (w == 0)
        return;
    if (t->wrap_pending)
    {
        t->wrap_pending = false;
        return;
    }
    if (t->cx >= w)
        t->cx -= w;
}

static void clear_line(term_t *t, int mode)
{
    uint32_t cw_ = cw(t), ch_ = ch(t);
    if (!t->fb || ch_ == 0 || cw_ == 0)
        return;

    switch (mode)
    {
    case 0:
        clear_rect(t, t->cx, t->cy, t->fb->width - t->cx, ch_, bg_col(t));
        break;
    case 1:
    {
        uint32_t w = t->cx + cw_;
        if (w > t->fb->width)
            w = t->fb->width;
        clear_rect(t, 0, t->cy, w, ch_, bg_col(t));
        break;
    }
    case 2:
        clear_rect(t, 0, t->cy, t->fb->width, ch_, bg_col(t));
        break;
    }
}

static void clear_screen(term_t *t, int mode)
{
    uint32_t ch_ = ch(t);
    if (!t->fb || ch_ == 0)
        return;

    switch (mode)
    {
    case 0:
        clear_line(t, 0);
        if (t->cy + ch_ < t->fb->height)
            clear_rect(t, 0, t->cy + ch_, t->fb->width,
                       t->fb->height - t->cy - ch_, bg_col(t));
        break;
    case 1:
        if (t->cy > 0)
            clear_rect(t, 0, 0, t->fb->width, t->cy, bg_col(t));
        clear_line(t, 1);
        break;
    case 2:
    case 3:
        clear_rect(t, 0, 0, t->fb->width, t->fb->height, bg_col(t));
        break;
    }
}

static void erase_chars(term_t *t, uint32_t n)
{
    uint32_t cw_ = cw(t), ch_ = ch(t);
    if (cw_ == 0 || ch_ == 0 || n == 0)
        return;
    clear_rect(t, t->cx, t->cy, cw_ * n, ch_, bg_col(t));
}

static void apply_sgr_simple(term_t *t, int p)
{
    if (p < 0)
        p = 0;

    switch (p)
    {
    case 0:
        reset_attrs(t);
        break;
    case 1:
        t->bold = true;
        break;
    case 2:
        break;
    case 22:
        t->bold = false;
        break;
    case 7:
        t->reverse = true;
        break;
    case 27:
        t->reverse = false;
        break;
    case 39:
        set_fg(t, TERM_DEFAULT_FG);
        break;
    case 49:
        set_bg(t, TERM_DEFAULT_BG);
        break;
    default:
        if (p >= 30 && p <= 37)
            set_fg(t, (uint16_t)(p - 30));
        if (p >= 40 && p <= 47)
            set_bg(t, (uint16_t)(p - 40));
        if (p >= 90 && p <= 97)
            set_fg(t, (uint16_t)(8 + p - 90));
        if (p >= 100 && p <= 107)
            set_bg(t, (uint16_t)(8 + p - 100));
        break;
    }
}

static void apply_sgr(term_t *t)
{
    if (t->ansi.param_count == 0)
    {
        reset_attrs(t);
        return;
    }

    for (uint32_t i = 0; i < t->ansi.param_count; i++)
    {
        int p = t->ansi.params[i];
        if (p < 0)
            p = 0;

        if ((p == 38 || p == 48) && i + 1 < t->ansi.param_count)
        {
            int mode = t->ansi.params[i + 1];
            if (mode < 0)
                continue;

            if (mode == 5 && i + 2 < t->ansi.param_count)
            {
                int c = t->ansi.params[i + 2];
                if (c >= 0 && c < 256)
                {
                    if (p == 38)
                        set_fg(t, (uint16_t)c);
                    else
                        set_bg(t, (uint16_t)c);
                }
                i += 2;
                continue;
            }

            if (mode == 2 && i + 4 < t->ansi.param_count)
            {
                int r = t->ansi.params[i + 2];
                int g = t->ansi.params[i + 3];
                int b = t->ansi.params[i + 4];
                if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255)
                {
                    if (p == 38)
                        set_fg_rgb(t, (uint32_t)r, (uint32_t)g, (uint32_t)b);
                    else
                        set_bg_rgb(t, (uint32_t)r, (uint32_t)g, (uint32_t)b);
                }
                i += 4;
                continue;
            }
        }

        apply_sgr_simple(t, p);
    }
}

static void private_mode(term_t *t, bool set)
{
    for (uint32_t i = 0; i < t->ansi.param_count; i++)
    {
        int p = t->ansi.params[i];
        if (p < 0)
            continue;

        switch (p)
        {
        case 7:
            t->wrap = set;
            t->wrap_pending = false;
            break;
        case 25:
            t->cursor_on = set;
            if (!set)
                t->cursor_drawn = false;
            break;
        }
    }
}

static void dispatch_sequence(term_t *t)
{
    if (t->ansi.simple)
    {
        switch (t->ansi.final)
        {
        case '7':
            save_cursor(t);
            break;
        case '8':
            restore_cursor(t);
            break;
        case 'D':
            index_(t);
            break;
        case 'E':
            newline(t);
            break;
        case 'c':
            term_reset(t);
            break;
        }
        return;
    }

    switch (t->ansi.final)
    {
    case 'm':
        apply_sgr(t);
        break;
    case 'A':
        cursor_up(t, (uint32_t)param(t, 0, 1));
        break;
    case 'B':
        cursor_down(t, (uint32_t)param(t, 0, 1));
        break;
    case 'C':
        cursor_forward(t, (uint32_t)param(t, 0, 1));
        break;
    case 'D':
        cursor_back(t, (uint32_t)param(t, 0, 1));
        break;
    case 'E':
        cursor_down(t, (uint32_t)param(t, 0, 1));
        t->cx = 0;
        break;
    case 'F':
        cursor_up(t, (uint32_t)param(t, 0, 1));
        t->cx = 0;
        break;

    case 'G':
    {
        uint32_t c = (uint32_t)param(t, 0, 1);
        if (c > 0)
            c--;
        set_cursor(t, row(t), c);
        break;
    }

    case 'd':
    {
        uint32_t r = (uint32_t)param(t, 0, 1);
        if (r > 0)
            r--;
        set_cursor(t, r, col(t));
        break;
    }

    case 'H':
    case 'f':
    {
        uint32_t r = (uint32_t)param(t, 0, 1);
        uint32_t c = (uint32_t)param(t, 1, 1);
        if (r > 0)
            r--;
        if (c > 0)
            c--;
        set_cursor(t, r, c);
        break;
    }

    case 'J':
        clear_screen(t, param(t, 0, 0));
        break;
    case 'K':
        clear_line(t, param(t, 0, 0));
        break;
    case 'X':
        erase_chars(t, (uint32_t)param(t, 0, 1));
        break;
    case 'S':
        scroll_lines(t, (uint32_t)param(t, 0, 1));
        break;
    case 's':
        save_cursor(t);
        break;
    case 'u':
        restore_cursor(t);
        break;

    default:
        if (t->ansi.private)
        {
            if (t->ansi.final == 'h')
                private_mode(t, true);
            if (t->ansi.final == 'l')
                private_mode(t, false);
        }
        break;
    }
}

static int param(term_t *t, uint32_t i, int fallback)
{
    return (i < t->ansi.param_count && t->ansi.params[i] >= 0)
               ? t->ansi.params[i]
               : fallback;
}

static void put_glyph(term_t *t, char c)
{
    uint32_t cw_ = cw(t), ch_ = ch(t);
    if (cw_ == 0 || ch_ == 0)
        return;
    if (cw_ > t->fb->width || ch_ > t->fb->height)
        return;

    if (t->wrap_pending)
    {
        if (t->wrap)
            newline(t);
        t->wrap_pending = false;
    }

    if (t->cx + cw_ > t->fb->width)
    {
        if (t->wrap)
            newline(t);
        else
            t->cx = t->fb->width - cw_;
    }

    ensure_visible(t);

    uint32_t idx = psf_glyph_index(&t->font, (unsigned char)c);

    volatile uint32_t *fb = pixels(t);
    uint32_t pt = pitch(t);
    uint32_t fg = fg_col(t);
    uint32_t bg = bg_col(t);

    clear_rect(t, t->cx, t->cy, cw_, ch_, bg);

    uint32_t gw = t->font.width;
    uint32_t gh = t->font.height;

    for (uint32_t r = 0; r < gh; r++)
    {
        const uint8_t *row = psf_glyph_row(&t->font, idx, r);
        uint32_t y = t->cy + r;
        if (y >= t->fb->height)
            continue;

        for (uint32_t xo = 0; xo < gw; xo++)
        {
            if (xo >= cw_)
                break;
            uint32_t x = t->cx + xo;
            if (x >= t->fb->width)
                continue;

            if ((row[xo / 8] >> (7 - (xo % 8))) & 1)
                fb[y * pt + x] = fg;
        }
    }

    uint32_t next = t->cx + cw_;
    if (next + cw_ > t->fb->width)
    {
        if (t->wrap)
            t->wrap_pending = true;
        else
            t->cx = t->fb->width - cw_;
    }
    else
    {
        t->cx = next;
    }
}

static void tab(term_t *t)
{
    uint32_t c = col(t);
    uint32_t spaces = TAB_WIDTH - (c % TAB_WIDTH);
    if (spaces == 0)
        spaces = TAB_WIDTH;
    for (uint32_t i = 0; i < spaces; i++)
        put_glyph(t, ' ');
}

static void term_reset(term_t *t)
{
    reset_attrs(t);
    t->cx = 0;
    t->cy = 0;
    t->saved_cx = 0;
    t->saved_cy = 0;
    t->wrap = true;
    t->wrap_pending = false;
    t->saved_wrap_pending = false;
    t->cursor_on = true;
    t->cursor_drawn = false;

    if (t->fb && t->fb->address)
        clear_rect(t, 0, 0, t->fb->width, t->fb->height, bg_col(t));
}

void term_init(term_t *t, struct limine_framebuffer *fb,
               const void *psf_data, size_t psf_size)
{
    t->fb = NULL;

    if (!psf_parse(psf_data, psf_size, &t->font))
        return;

    t->fb = fb;
    t->cx = 0;
    t->cy = 0;
    t->wrap = true;
    t->cursor_on = true;
    ansi_init(&t->ansi);
    reset_attrs(t);

    if (t->fb && t->fb->address)
        clear_rect(t, 0, 0, t->fb->width, t->fb->height, ansi_color(TERM_DEFAULT_BG));

    cursor_show(t);
}

void term_putc(term_t *t, char c)
{
    if (!t->fb || !t->fb->address)
        return;
    if (t->font.glyph_data == NULL)
        return;
    if (cw(t) == 0 || ch(t) == 0)
        return;

    cursor_hide(t);

    switch (ansi_feed(&t->ansi, c))
    {
    case ANSI_PENDING:
        cursor_show(t);
        return;
    case ANSI_READY:
        dispatch_sequence(t);
        cursor_show(t);
        return;
    default:
        break;
    }

    switch (c)
    {
    case '\n':
        newline(t);
        break;
    case '\r':
        t->cx = 0;
        t->wrap_pending = false;
        break;
    case '\b':
        backspace(t);
        break;
    case '\t':
        tab(t);
        break;
    default:
        if ((unsigned char)c >= 0x20 && (unsigned char)c != 0x7f)
            put_glyph(t, c);
        break;
    }

    cursor_show(t);
}

void term_puts(term_t *t, const char *s)
{
    if (!s)
        return;
    while (*s)
        term_putc(t, *s++);
}
