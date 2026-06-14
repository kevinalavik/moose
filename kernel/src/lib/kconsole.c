#include <lib/kconsole.h>
#include <lib/printk.h>
#include <stddef.h>
#include <stdint.h>
#include <arch/fastmem.h>
#define ASCII_FONT_IMPLEMENTATION
#include <extra/ascii_font.h>

static struct limine_framebuffer *fb;
static unsigned long cx, cy;

static uint32_t col_fg = KCONSOLE_DEFAULT_FG;
static uint32_t col_bg = KCONSOLE_DEFAULT_BG;

static uint32_t mapped_fg;
static uint32_t mapped_bg;
static uint32_t mapped_cursor;

static inline uint32_t _map_rgb(uint32_t rgb)
{
	uint8_t r = (rgb >> 16) & 0xFF;
	uint8_t g = (rgb >> 8) & 0xFF;
	uint8_t b = (rgb) & 0xFF;
	return ((uint32_t)r << fb->red_mask_shift) | ((uint32_t)g << fb->green_mask_shift) |
	       ((uint32_t)b << fb->blue_mask_shift);
}

static void _refresh_mapped_colors(void)
{
	if (!fb)
		return;
	mapped_fg = _map_rgb(col_fg);
	mapped_bg = _map_rgb(col_bg);
	uint8_t fr = (col_fg >> 16) & 0xFF, fg_ = (col_fg >> 8) & 0xFF, fb_ = col_fg & 0xFF;
	uint8_t br = (col_bg >> 16) & 0xFF, bg_ = (col_bg >> 8) & 0xFF, bb_ = col_bg & 0xFF;
	uint8_t cr = (uint8_t)((fr * 179 + br * 77) >> 8);
	uint8_t cg = (uint8_t)((fg_ * 179 + bg_ * 77) >> 8);
	uint8_t cb = (uint8_t)((fb_ * 179 + bb_ * 77) >> 8);
	mapped_cursor = ((uint32_t)cr << fb->red_mask_shift) |
	                ((uint32_t)cg << fb->green_mask_shift) |
	                ((uint32_t)cb << fb->blue_mask_shift);
}

static inline uint32_t *_pixel_ptr(uint64_t x, uint64_t y)
{
	return (uint32_t *)((uint8_t *)fb->address + y * fb->pitch + x * (fb->bpp / 8));
}

static void _cursor(uint32_t c)
{
	for (int y = 0; y < ASCII_FONT_HEIGHT; y++)
		fast_memset_row(_pixel_ptr(cx, cy + y), c, ASCII_FONT_WIDTH);
}


static void _scroll(void)
{
	uint64_t text_h = fb->height - ASCII_FONT_HEIGHT;
	uint64_t row = (uint64_t)ASCII_FONT_HEIGHT * fb->pitch;
	uint64_t copy = (text_h * fb->pitch) - row;
	fast_memcpy_fwd(fb->address, (uint8_t *)fb->address + row, copy);
	uint64_t last_y = text_h - ASCII_FONT_HEIGHT;
	for (uint64_t y = last_y; y < text_h; y++) {
		fast_memset_row(_pixel_ptr(0, y), mapped_bg, fb->width);
	}

	if (cy >= ASCII_FONT_HEIGHT)
		cy -= ASCII_FONT_HEIGHT;
}

static void _drawch_xy(char ch, uint64_t x, uint64_t y)
{
	const unsigned char(*g)[ASCII_FONT_WIDTH] = ascii_font[(unsigned char)ch];
	for (unsigned int row = 0; row < ASCII_FONT_HEIGHT; row++) {
		uint32_t row_buf[ASCII_FONT_WIDTH];
		for (unsigned int col = 0; col < ASCII_FONT_WIDTH; col++) {
			uint8_t v = g[row][col];
			if (v == 0) {
				row_buf[col] = mapped_bg;
			} else if (v == 255) {
				row_buf[col] = mapped_fg;
			} else {
				uint8_t fr = (col_fg >> 16) & 0xFF;
				uint8_t fg_ = (col_fg >> 8) & 0xFF;
				uint8_t fb_ = (col_fg) & 0xFF;
				uint8_t br = (col_bg >> 16) & 0xFF;
				uint8_t bg_ = (col_bg >> 8) & 0xFF;
				uint8_t bb_ = (col_bg) & 0xFF;
				uint8_t r =
				    (uint8_t)(((uint16_t)fr * v + (uint16_t)br * (255 - v)) >> 8);
				uint8_t g2 =
				    (uint8_t)(((uint16_t)fg_ * v + (uint16_t)bg_ * (255 - v)) >> 8);
				uint8_t b =
				    (uint8_t)(((uint16_t)fb_ * v + (uint16_t)bb_ * (255 - v)) >> 8);
				row_buf[col] = ((uint32_t)r << fb->red_mask_shift) |
				               ((uint32_t)g2 << fb->green_mask_shift) |
				               ((uint32_t)b << fb->blue_mask_shift);
			}
		}
		fast_memcpy_row(_pixel_ptr(x, y + row), row_buf, ASCII_FONT_WIDTH);
	}
}

static void _drawch(char ch)
{
	uint64_t text_h = fb->height - ASCII_FONT_HEIGHT;
	if (cy + ASCII_FONT_HEIGHT >= text_h) {
		_scroll();
	}

	_drawch_xy(ch, cx, cy);
	cx += ASCII_FONT_WIDTH;
	if (cx + ASCII_FONT_WIDTH >= fb->width) {
		cx = 0;
		cy += ASCII_FONT_HEIGHT;
	}
}
void kconsole_draw_bar(void)
{
	if (!fb)
		return;

	uint32_t save_fg = col_fg;
	uint32_t save_bg = col_bg;

	col_fg = save_bg;
	col_bg = save_fg;
	_refresh_mapped_colors();

	uint32_t bar_y = fb->height - ASCII_FONT_HEIGHT;

	for (unsigned int y = 0; y < ASCII_FONT_HEIGHT; y++)
		fast_memset_row(_pixel_ptr(0, bar_y + y), mapped_bg, fb->width);

	char left[64];
	snprintk(left, sizeof(left), " Copyright (c) Kevin Alavik 2026");

	uint64_t draw_x = 0;
	for (int i = 0; left[i] && draw_x + ASCII_FONT_WIDTH <= fb->width; i++) {
		_drawch_xy(left[i], draw_x, bar_y);
		draw_x += ASCII_FONT_WIDTH;
	}

	char right[64];
	snprintk(right,
	         sizeof(right),
	         "moose-kernel v%d.%d.%d%s on kconsole ",
	         VER_MAJOR,
	         VER_MINOR,
	         VER_PATCH,
	         VER_NOTE);
	unsigned long rlen = 0;
	while (right[rlen])
		rlen++;
	unsigned long right_px = rlen * ASCII_FONT_WIDTH;
	if (right_px <= fb->width) {
		draw_x = fb->width - right_px;
		for (unsigned int i = 0; right[i]; i++) {
			_drawch_xy(right[i], draw_x, bar_y);
			draw_x += ASCII_FONT_WIDTH;
		}
	}

	col_fg = save_fg;
	col_bg = save_bg;
	_refresh_mapped_colors();
}

void kconsole_init(struct limine_framebuffer *f)
{
	fb = f;
	cx = cy = 0;
	_refresh_mapped_colors();
	for (uint64_t y = 0; y < fb->height; y++) {
		fast_memset_row(_pixel_ptr(0, y), mapped_bg, fb->width);
	}
	kconsole_draw_bar();
}

void kconsole_deinit(void)
{
	if (!fb)
		return;
	for (uint64_t y = 0; y < fb->height; y++)
		fast_memset_row(_pixel_ptr(0, y), 0, fb->width);
	fb = NULL;
}

void kconsole_set_fg(uint32_t rgb)
{
	col_fg = rgb & 0x00FFFFFF;
	_refresh_mapped_colors();
}

void kconsole_set_bg(uint32_t rgb)
{
	col_bg = rgb & 0x00FFFFFF;
	_refresh_mapped_colors();
}

void kconsole_write(const char *s)
{
	if (!fb)
		return;

	_cursor(mapped_bg);
	while (*s) {
		if (*s == '\n') {
			cx = 0;
			cy += ASCII_FONT_HEIGHT;
			s++;
		} else if (*s == '\t') {
			unsigned long col = cx / ASCII_FONT_WIDTH;
			unsigned long next_col = (col + 4) & ~3u;
			unsigned long new_cx = next_col * ASCII_FONT_WIDTH;
			unsigned long fill_pixels = new_cx - cx;
			if (new_cx < fb->width && fill_pixels > 0) {
				for (int y = 0; y < ASCII_FONT_HEIGHT; y++)
					fast_memset_row(
					    _pixel_ptr(cx, cy + y), mapped_bg, fill_pixels);
				cx = new_cx;
			} else {
				unsigned long tail = fb->width - cx;
				if (tail > 0)
					for (int y = 0; y < ASCII_FONT_HEIGHT; y++)
						fast_memset_row(
						    _pixel_ptr(cx, cy + y), mapped_bg, tail);
				cx = 0;
				cy += ASCII_FONT_HEIGHT;
			}
			s++;
		} else if (*s == '\b') {
			cx -= ASCII_FONT_WIDTH;
			s++;
		} else {
			_drawch(*s++);
		}

		if (cy >= fb->height - ASCII_FONT_HEIGHT)
			_scroll();
	}
	_cursor(mapped_cursor);
}