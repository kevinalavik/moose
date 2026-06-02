#include <lib/term.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define TERM_DEFAULT_FG 7
#define TERM_DEFAULT_BG 0

#define ANSI_PARAM_CAP 16
#define TAB_WIDTH 8
#define CURSOR_MASK 0x00ffffff

enum ansi_state
{
    ANSI_NORMAL,
    ANSI_ESC,
    ANSI_CSI,
};

struct terminal
{
    struct limine_framebuffer *fb;
    const BDF_Font *font;

    uint32_t cx;
    uint32_t cy;

    uint32_t saved_cx;
    uint32_t saved_cy;

    uint16_t fg;
    uint16_t bg;
    uint32_t fg_rgb;
    uint32_t bg_rgb;
    bool fg_is_rgb;
    bool bg_is_rgb;

    bool bold;
    bool reverse;
    bool wrap;
    bool wrap_pending;

    bool saved_wrap_pending;

    bool cursor_on;
    bool cursor_drawn;

    enum ansi_state ansi_state;
    int ansi_params[ANSI_PARAM_CAP];
    uint32_t ansi_param_count;
    uint32_t ansi_value;
    bool ansi_have_value;
    bool ansi_private;
};

static struct terminal term;

static const uint32_t ansi16[16] = {
    0x001e1e2e,
    0x00f38ba8,
    0x00a6e3a1,
    0x00f9e2af,
    0x0089b4fa,
    0x00cba6f7,
    0x0094e2d5,
    0x00cdd6f4,
    0x00585b70,
    0x00eba0ac,
    0x00b4f9c0,
    0x00fce8b2,
    0x00a6c8ff,
    0x00d8b4fe,
    0x00aef3e7,
    0x00f5f7ff,
};

static uint32_t rgb(uint32_t r, uint32_t g, uint32_t b)
{
    return ((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff);
}

static uint32_t cube_value(uint32_t n)
{
    if (n == 0)
        return 0;

    return 55 + n * 40;
}

static uint32_t ansi_color(uint16_t n)
{
    if (n < 16)
        return ansi16[n];

    if (n < 232)
    {
        uint32_t v = n - 16;
        uint32_t r = v / 36;
        uint32_t g = (v / 6) % 6;
        uint32_t b = v % 6;

        return rgb(cube_value(r), cube_value(g), cube_value(b));
    }

    if (n < 256)
    {
        uint32_t v = 8 + (n - 232) * 10;

        return rgb(v, v, v);
    }

    return ansi16[TERM_DEFAULT_FG];
}

static volatile uint32_t *term_pixels(void)
{
    return (volatile uint32_t *)term.fb->address;
}

static uint32_t term_pitch(void)
{
    return (uint32_t)(term.fb->pitch / sizeof(uint32_t));
}

static uint32_t term_cw(void)
{
    return (uint32_t)term.font->bbox.w;
}

static uint32_t term_ch(void)
{
    return (uint32_t)term.font->bbox.h;
}

static uint32_t term_cols(void)
{
    uint32_t cw = term_cw();

    if (cw == 0)
        return 0;

    return (uint32_t)term.fb->width / cw;
}

static uint32_t term_rows(void)
{
    uint32_t ch = term_ch();

    if (ch == 0)
        return 0;

    return (uint32_t)term.fb->height / ch;
}

static uint16_t term_effective_fg(void)
{
    if (term.bold && term.fg < 8)
        return term.fg + 8;

    return term.fg;
}

static uint32_t term_indexed_fg_color(void)
{
    return ansi_color(term_effective_fg());
}

static uint32_t term_indexed_bg_color(void)
{
    return ansi_color(term.bg);
}

static uint32_t term_real_fg_color(void)
{
    if (term.fg_is_rgb)
        return term.fg_rgb;

    return term_indexed_fg_color();
}

static uint32_t term_real_bg_color(void)
{
    if (term.bg_is_rgb)
        return term.bg_rgb;

    return term_indexed_bg_color();
}

static uint32_t term_fg_color(void)
{
    if (term.reverse)
        return term_real_bg_color();

    return term_real_fg_color();
}

static uint32_t term_bg_color(void)
{
    if (term.reverse)
        return term_real_fg_color();

    return term_real_bg_color();
}

static void term_set_fg(uint16_t n)
{
    if (n < 256)
    {
        term.fg = n;
        term.fg_is_rgb = false;
    }
}

static void term_set_bg(uint16_t n)
{
    if (n < 256)
    {
        term.bg = n;
        term.bg_is_rgb = false;
    }
}

static void term_set_fg_rgb(uint32_t r, uint32_t g, uint32_t b)
{
    if (r <= 255 && g <= 255 && b <= 255)
    {
        term.fg_rgb = rgb(r, g, b);
        term.fg_is_rgb = true;
    }
}

static void term_set_bg_rgb(uint32_t r, uint32_t g, uint32_t b)
{
    if (r <= 255 && g <= 255 && b <= 255)
    {
        term.bg_rgb = rgb(r, g, b);
        term.bg_is_rgb = true;
    }
}

static void term_reset_attrs(void)
{
    term.fg = TERM_DEFAULT_FG;
    term.bg = TERM_DEFAULT_BG;
    term.fg_rgb = 0;
    term.bg_rgb = 0;
    term.fg_is_rgb = false;
    term.bg_is_rgb = false;
    term.bold = false;
    term.reverse = false;
}

static void term_clear_rect(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint32_t color)
{
    if (!term.fb || !term.fb->address)
        return;

    if (x0 >= term.fb->width || y0 >= term.fb->height)
        return;

    if (x0 + w > term.fb->width)
        w = term.fb->width - x0;

    if (y0 + h > term.fb->height)
        h = term.fb->height - y0;

    volatile uint32_t *pixels = term_pixels();
    uint32_t pitch = term_pitch();

    for (uint32_t y = 0; y < h; y++)
    {
        volatile uint32_t *row = pixels + (y0 + y) * pitch + x0;

        for (uint32_t x = 0; x < w; x++)
            row[x] = color;
    }
}

static void term_scroll_pixels(uint32_t amount)
{
    if (!term.fb || !term.fb->address)
        return;

    if (amount == 0)
        return;

    if (amount >= term.fb->height)
    {
        term_clear_rect(0, 0, term.fb->width, term.fb->height, term_bg_color());
        return;
    }

    volatile uint32_t *pixels = term_pixels();
    uint32_t pitch = term_pitch();
    uint32_t h = term.fb->height - amount;

    for (uint32_t y = 0; y < h; y++)
    {
        volatile uint32_t *dst = pixels + y * pitch;
        volatile uint32_t *src = pixels + (y + amount) * pitch;

        for (uint32_t x = 0; x < term.fb->width; x++)
            dst[x] = src[x];
    }

    term_clear_rect(0, h, term.fb->width, amount, term_bg_color());
}

static void term_scroll_lines(uint32_t n)
{
    uint32_t ch = term_ch();

    if (ch == 0 || n == 0)
        return;

    term_scroll_pixels(ch * n);
}

static void term_ensure_visible(void)
{
    uint32_t ch = term_ch();

    if (!term.fb || ch == 0)
        return;

    if (ch > term.fb->height)
    {
        term.cy = 0;
        return;
    }

    while (term.cy + ch > term.fb->height)
    {
        term_scroll_lines(1);

        if (term.cy >= ch)
            term.cy -= ch;
        else
            term.cy = 0;
    }
}

static void term_cursor_flip(void)
{
    if (!term.fb || !term.fb->address || !term.font)
        return;

    uint32_t cw = term_cw();
    uint32_t ch = term_ch();

    if (cw == 0 || ch == 0)
        return;

    if (term.cx + cw > term.fb->width || term.cy + ch > term.fb->height)
        return;

    volatile uint32_t *pixels = term_pixels();
    uint32_t pitch = term_pitch();

    for (uint32_t y = 0; y < ch; y++)
    {
        volatile uint32_t *row = pixels + (term.cy + y) * pitch + term.cx;

        for (uint32_t x = 0; x < cw; x++)
            row[x] ^= CURSOR_MASK;
    }
}

static void term_cursor_hide(void)
{
    if (!term.cursor_drawn)
        return;

    term_cursor_flip();
    term.cursor_drawn = false;
}

static void term_cursor_show(void)
{
    if (!term.cursor_on || term.cursor_drawn)
        return;

    term_ensure_visible();
    term_cursor_flip();
    term.cursor_drawn = true;
}

static uint32_t term_col(void)
{
    uint32_t cw = term_cw();

    if (cw == 0)
        return 0;

    return term.cx / cw;
}

static uint32_t term_row(void)
{
    uint32_t ch = term_ch();

    if (ch == 0)
        return 0;

    return term.cy / ch;
}

static void term_set_cursor(uint32_t row, uint32_t col)
{
    uint32_t rows = term_rows();
    uint32_t cols = term_cols();

    if (rows == 0 || cols == 0)
        return;

    if (row >= rows)
        row = rows - 1;

    if (col >= cols)
        col = cols - 1;

    term.cx = col * term_cw();
    term.cy = row * term_ch();
    term.wrap_pending = false;
}

static void term_save_cursor(void)
{
    term.saved_cx = term.cx;
    term.saved_cy = term.cy;
    term.saved_wrap_pending = term.wrap_pending;
}

static void term_restore_cursor(void)
{
    term.cx = term.saved_cx;
    term.cy = term.saved_cy;
    term.wrap_pending = term.saved_wrap_pending;
    term_ensure_visible();
}

static void term_cursor_up(uint32_t n)
{
    uint32_t row = term_row();
    uint32_t col = term_col();

    if (n > row)
        row = 0;
    else
        row -= n;

    term_set_cursor(row, col);
}

static void term_cursor_down(uint32_t n)
{
    uint32_t row = term_row();
    uint32_t col = term_col();
    uint32_t rows = term_rows();

    if (rows == 0)
        return;

    row += n;

    if (row >= rows)
        row = rows - 1;

    term_set_cursor(row, col);
}

static void term_cursor_forward(uint32_t n)
{
    uint32_t row = term_row();
    uint32_t col = term_col();
    uint32_t cols = term_cols();

    if (cols == 0)
        return;

    col += n;

    if (col >= cols)
        col = cols - 1;

    term_set_cursor(row, col);
}

static void term_cursor_back(uint32_t n)
{
    uint32_t row = term_row();
    uint32_t col = term_col();

    if (n > col)
        col = 0;
    else
        col -= n;

    term_set_cursor(row, col);
}

static void term_newline(void)
{
    uint32_t ch = term_ch();

    if (ch == 0)
        return;

    term.cx = 0;
    term.cy += ch;
    term.wrap_pending = false;

    term_ensure_visible();
}

static void term_index(void)
{
    uint32_t ch = term_ch();

    if (ch == 0)
        return;

    term.cy += ch;
    term.wrap_pending = false;

    term_ensure_visible();
}

static void term_backspace(void)
{
    uint32_t cw = term_cw();

    if (cw == 0)
        return;

    if (term.wrap_pending)
    {
        term.wrap_pending = false;
        return;
    }

    if (term.cx >= cw)
        term.cx -= cw;
}

static void term_clear_line(int mode)
{
    uint32_t ch = term_ch();
    uint32_t cw = term_cw();

    if (!term.fb || ch == 0 || cw == 0)
        return;

    if (mode == 0)
    {
        term_clear_rect(term.cx, term.cy, term.fb->width - term.cx, ch, term_bg_color());
    }
    else if (mode == 1)
    {
        uint32_t w = term.cx + cw;

        if (w > term.fb->width)
            w = term.fb->width;

        term_clear_rect(0, term.cy, w, ch, term_bg_color());
    }
    else if (mode == 2)
    {
        term_clear_rect(0, term.cy, term.fb->width, ch, term_bg_color());
    }
}

static void term_clear_screen(int mode)
{
    uint32_t ch = term_ch();

    if (!term.fb || ch == 0)
        return;

    if (mode == 0)
    {
        term_clear_line(0);

        if (term.cy + ch < term.fb->height)
            term_clear_rect(0, term.cy + ch, term.fb->width, term.fb->height - term.cy - ch, term_bg_color());
    }
    else if (mode == 1)
    {
        if (term.cy > 0)
            term_clear_rect(0, 0, term.fb->width, term.cy, term_bg_color());

        term_clear_line(1);
    }
    else if (mode == 2 || mode == 3)
    {
        term_clear_rect(0, 0, term.fb->width, term.fb->height, term_bg_color());
    }
}

static void term_erase_chars(uint32_t n)
{
    uint32_t cw = term_cw();
    uint32_t ch = term_ch();

    if (cw == 0 || ch == 0 || n == 0)
        return;

    term_clear_rect(term.cx, term.cy, cw * n, ch, term_bg_color());
}

static void term_reset(void)
{
    term_reset_attrs();

    term.cx = 0;
    term.cy = 0;

    term.saved_cx = 0;
    term.saved_cy = 0;

    term.wrap = true;
    term.wrap_pending = false;
    term.saved_wrap_pending = false;

    term.cursor_on = true;
    term.cursor_drawn = false;

    if (term.fb && term.fb->address)
        term_clear_rect(0, 0, term.fb->width, term.fb->height, term_bg_color());
}

void term_init(struct limine_framebuffer *fb, const BDF_Font *font)
{
    term = (struct terminal){0};

    term.fb = fb;
    term.font = font;

    term.wrap = true;
    term.cursor_on = true;

    term_reset_attrs();

    if (term.fb && term.fb->address)
        term_clear_rect(0, 0, term.fb->width, term.fb->height, ansi_color(TERM_DEFAULT_BG));

    term_cursor_show();
}

static void ansi_push_param(void)
{
    if (term.ansi_param_count < ANSI_PARAM_CAP)
    {
        if (term.ansi_have_value)
            term.ansi_params[term.ansi_param_count++] = (int)term.ansi_value;
        else
            term.ansi_params[term.ansi_param_count++] = -1;
    }

    term.ansi_value = 0;
    term.ansi_have_value = false;
}

static int ansi_param(uint32_t i, int fallback)
{
    if (i >= term.ansi_param_count)
        return fallback;

    if (term.ansi_params[i] < 0)
        return fallback;

    return term.ansi_params[i];
}

static void ansi_apply_sgr_simple(int p)
{
    if (p < 0)
        p = 0;

    if (p == 0)
    {
        term_reset_attrs();
        return;
    }

    if (p == 1)
    {
        term.bold = true;
        return;
    }

    if (p == 2)
    {
        return;
    }

    if (p == 22)
    {
        term.bold = false;
        return;
    }

    if (p == 7)
    {
        term.reverse = true;
        return;
    }

    if (p == 27)
    {
        term.reverse = false;
        return;
    }

    if (p == 39)
    {
        term_set_fg(TERM_DEFAULT_FG);
        return;
    }

    if (p == 49)
    {
        term_set_bg(TERM_DEFAULT_BG);
        return;
    }

    if (p >= 30 && p <= 37)
    {
        term_set_fg((uint16_t)(p - 30));
        return;
    }

    if (p >= 40 && p <= 47)
    {
        term_set_bg((uint16_t)(p - 40));
        return;
    }

    if (p >= 90 && p <= 97)
    {
        term_set_fg((uint16_t)(8 + p - 90));
        return;
    }

    if (p >= 100 && p <= 107)
    {
        term_set_bg((uint16_t)(8 + p - 100));
        return;
    }
}

static void ansi_apply_sgr(void)
{
    if (term.ansi_param_count == 0)
    {
        term_reset_attrs();
        return;
    }

    for (uint32_t i = 0; i < term.ansi_param_count; i++)
    {
        int p = ansi_param(i, 0);

        if ((p == 38 || p == 48) && i + 1 < term.ansi_param_count)
        {
            int mode = ansi_param(i + 1, -1);

            if (mode == 5 && i + 2 < term.ansi_param_count)
            {
                int color = ansi_param(i + 2, -1);

                if (color >= 0 && color < 256)
                {
                    if (p == 38)
                        term_set_fg((uint16_t)color);
                    else
                        term_set_bg((uint16_t)color);
                }

                i += 2;
                continue;
            }

            if (mode == 2 && i + 4 < term.ansi_param_count)
            {
                int r = ansi_param(i + 2, -1);
                int g = ansi_param(i + 3, -1);
                int b = ansi_param(i + 4, -1);

                if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255)
                {
                    if (p == 38)
                        term_set_fg_rgb((uint32_t)r, (uint32_t)g, (uint32_t)b);
                    else
                        term_set_bg_rgb((uint32_t)r, (uint32_t)g, (uint32_t)b);
                }

                i += 4;
                continue;
            }
        }

        ansi_apply_sgr_simple(p);
    }
}

static void ansi_private_mode(bool set)
{
    for (uint32_t i = 0; i < term.ansi_param_count; i++)
    {
        int p = ansi_param(i, 0);

        if (p == 7)
        {
            term.wrap = set;
            term.wrap_pending = false;
        }
        else if (p == 25)
        {
            term.cursor_on = set;

            if (!set)
                term.cursor_drawn = false;
        }
    }
}

static void ansi_csi_final(char c)
{
    ansi_push_param();

    if (term.ansi_private)
    {
        if (c == 'h')
            ansi_private_mode(true);
        else if (c == 'l')
            ansi_private_mode(false);

        return;
    }

    if (c == 'm')
    {
        ansi_apply_sgr();
        return;
    }

    if (c == 'A')
    {
        term_cursor_up((uint32_t)ansi_param(0, 1));
        return;
    }

    if (c == 'B')
    {
        term_cursor_down((uint32_t)ansi_param(0, 1));
        return;
    }

    if (c == 'C')
    {
        term_cursor_forward((uint32_t)ansi_param(0, 1));
        return;
    }

    if (c == 'D')
    {
        term_cursor_back((uint32_t)ansi_param(0, 1));
        return;
    }

    if (c == 'E')
    {
        term_cursor_down((uint32_t)ansi_param(0, 1));
        term.cx = 0;
        return;
    }

    if (c == 'F')
    {
        term_cursor_up((uint32_t)ansi_param(0, 1));
        term.cx = 0;
        return;
    }

    if (c == 'G')
    {
        uint32_t col = (uint32_t)ansi_param(0, 1);

        if (col > 0)
            col--;

        term_set_cursor(term_row(), col);
        return;
    }

    if (c == 'd')
    {
        uint32_t row = (uint32_t)ansi_param(0, 1);

        if (row > 0)
            row--;

        term_set_cursor(row, term_col());
        return;
    }

    if (c == 'H' || c == 'f')
    {
        uint32_t row = (uint32_t)ansi_param(0, 1);
        uint32_t col = (uint32_t)ansi_param(1, 1);

        if (row > 0)
            row--;

        if (col > 0)
            col--;

        term_set_cursor(row, col);
        return;
    }

    if (c == 'J')
    {
        term_clear_screen(ansi_param(0, 0));
        return;
    }

    if (c == 'K')
    {
        term_clear_line(ansi_param(0, 0));
        return;
    }

    if (c == 'X')
    {
        term_erase_chars((uint32_t)ansi_param(0, 1));
        return;
    }

    if (c == 'S')
    {
        term_scroll_lines((uint32_t)ansi_param(0, 1));
        return;
    }

    if (c == 's')
    {
        term_save_cursor();
        return;
    }

    if (c == 'u')
    {
        term_restore_cursor();
        return;
    }
}

static void ansi_reset_parser(void)
{
    term.ansi_state = ANSI_NORMAL;
    term.ansi_param_count = 0;
    term.ansi_value = 0;
    term.ansi_have_value = false;
    term.ansi_private = false;
}

static bool ansi_consume(char c)
{
    switch (term.ansi_state)
    {
    case ANSI_NORMAL:
        if ((unsigned char)c == 0x1b)
        {
            term.ansi_state = ANSI_ESC;
            return true;
        }

        return false;

    case ANSI_ESC:
        if (c == '[')
        {
            term.ansi_state = ANSI_CSI;
            term.ansi_param_count = 0;
            term.ansi_value = 0;
            term.ansi_have_value = false;
            term.ansi_private = false;
            return true;
        }

        if (c == '7')
        {
            term_save_cursor();
            ansi_reset_parser();
            return true;
        }

        if (c == '8')
        {
            term_restore_cursor();
            ansi_reset_parser();
            return true;
        }

        if (c == 'D')
        {
            term_index();
            ansi_reset_parser();
            return true;
        }

        if (c == 'E')
        {
            term_newline();
            ansi_reset_parser();
            return true;
        }

        if (c == 'c')
        {
            term_reset();
            ansi_reset_parser();
            return true;
        }

        ansi_reset_parser();
        return true;

    case ANSI_CSI:
        if (c == '?' && term.ansi_param_count == 0 && !term.ansi_have_value)
        {
            term.ansi_private = true;
            return true;
        }

        if (c >= '0' && c <= '9')
        {
            term.ansi_have_value = true;
            term.ansi_value = term.ansi_value * 10 + (uint32_t)(c - '0');
            return true;
        }

        if (c == ';' || c == ':')
        {
            ansi_push_param();
            return true;
        }

        if (c >= 0x40 && c <= 0x7e)
        {
            ansi_csi_final(c);
            ansi_reset_parser();
            return true;
        }

        ansi_reset_parser();
        return true;
    }

    return false;
}

static void term_put_glyph(char c)
{
    uint32_t cw = term_cw();
    uint32_t ch = term_ch();

    if (cw == 0 || ch == 0)
        return;

    if (cw > term.fb->width || ch > term.fb->height)
        return;

    if (term.wrap_pending)
    {
        if (term.wrap)
            term_newline();

        term.wrap_pending = false;
    }

    if (term.cx + cw > term.fb->width)
    {
        if (term.wrap)
            term_newline();
        else
            term.cx = term.fb->width - cw;
    }

    term_ensure_visible();

    const BDF_Glyph *glyph =
        bdf_glyph_for_codepoint(term.font, (unsigned char)c);

    if (!glyph)
        glyph = &term.font->glyphs[0];

    volatile uint32_t *pixels = term_pixels();
    uint32_t pitch = term_pitch();

    uint32_t fg = term_fg_color();
    uint32_t bg = term_bg_color();

    term_clear_rect(term.cx, term.cy, cw, ch, bg);

    uint8_t row[32];

    for (int32_t r = 0; r < glyph->bbox.h; r++)
    {
        if (bdf_bitmap_row(glyph, (uint32_t)r, row, sizeof(row)) < 0)
            continue;

        uint32_t y = term.cy + (uint32_t)r;

        if (y >= term.fb->height)
            continue;

        for (int32_t col = 0; col < glyph->bbox.w; col++)
        {
            if ((uint32_t)col >= cw)
                break;

            uint32_t x = term.cx + (uint32_t)col;

            if (x >= term.fb->width)
                continue;

            uint32_t on = (row[col / 8] >> (7 - (col % 8))) & 1;

            if (on)
                pixels[y * pitch + x] = fg;
        }
    }

    uint32_t next = term.cx + cw;

    if (next + cw > term.fb->width)
    {
        if (term.wrap)
            term.wrap_pending = true;
        else
            term.cx = term.fb->width - cw;
    }
    else
    {
        term.cx = next;
    }
}

static void term_tab(void)
{
    uint32_t col = term_col();
    uint32_t spaces = TAB_WIDTH - (col % TAB_WIDTH);

    if (spaces == 0)
        spaces = TAB_WIDTH;

    for (uint32_t i = 0; i < spaces; i++)
        term_put_glyph(' ');
}

void term_putc(char c)
{
    if (!term.fb || !term.fb->address || !term.font)
        return;

    if (term_cw() == 0 || term_ch() == 0)
        return;

    term_cursor_hide();

    if (ansi_consume(c))
    {
        term_cursor_show();
        return;
    }

    if (c == '\n')
    {
        term_newline();
        term_cursor_show();
        return;
    }

    if (c == '\r')
    {
        term.cx = 0;
        term.wrap_pending = false;
        term_cursor_show();
        return;
    }

    if (c == '\b')
    {
        term_backspace();
        term_cursor_show();
        return;
    }

    if (c == '\t')
    {
        term_tab();
        term_cursor_show();
        return;
    }

    if ((unsigned char)c >= 0x20 && (unsigned char)c != 0x7f)
        term_put_glyph(c);

    term_cursor_show();
}

void term_puts(const char *s)
{
    if (!s)
        return;

    while (*s)
        term_putc(*s++);
}