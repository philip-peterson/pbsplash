/* SPDX-License-Identifier: BSD-2-Clause */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pbsplash.h"
#include "framebuffer.h"
#include "tfblib.h"

extern inline uint32_t tfb_make_color(uint8_t red, uint8_t green, uint8_t blue);
extern inline void tfb_draw_pixel(int x, int y, uint32_t color);
extern inline uint32_t tfb_screen_width(void);
extern inline uint32_t tfb_screen_height(void);
extern inline uint32_t tfb_win_width(void);
extern inline uint32_t tfb_win_height(void);

void *__fb_buffer;
void *__fb_real_buffer;
size_t __fb_size;
size_t __fb_pitch;
size_t __fb_pitch_div4; /*
			 * Used in tfb_draw_pixel* to save a (x << 2) operation.
			 * If we had to use __fb_pitch, we'd had to write:
			 *    *(uint32_t *)(__fb_buffer + (x << 2) + y * __fb_pitch)
			 * which clearly requires an additional shift operation
			 * that we can skip by using __fb_pitch_div4 + an early
			 * cast to uint32_t.
			 */

int __fb_screen_w;
int __fb_screen_h;
int __fb_win_w;
int __fb_win_h;
int __fb_off_x;
int __fb_off_y;
int __fb_win_end_x;
int __fb_win_end_y;

uint32_t __fb_r_mask;
uint32_t __fb_g_mask;
uint32_t __fb_b_mask;
uint8_t __fb_r_mask_size;
uint8_t __fb_g_mask_size;
uint8_t __fb_b_mask_size;
uint8_t __fb_r_pos;
uint8_t __fb_g_pos;
uint8_t __fb_b_pos;

void tfb_clear_screen(uint32_t color)
{
	if (__fb_pitch == (uint32_t)4 * __fb_screen_w) {
		memset(__fb_buffer, color, __fb_size >> 2);
		return;
	}

	for (int y = 0; y < __fb_screen_h; y++)
		tfb_draw_hline(0, y, __fb_screen_w, color);
}

void tfb_clear_win(uint32_t color)
{
	tfb_fill_rect(0, 0, __fb_win_w, __fb_win_h, color);
}

void tfb_draw_hline(int x, int y, int len, uint32_t color)
{
	if (x < 0) {
		len += x;
		x = 0;
	}

	x += __fb_off_x;
	y += __fb_off_y;

	if (len < 0 || y < __fb_off_y || y >= __fb_win_end_y)
		return;

	len = MIN(len, MAX(0, (int)__fb_win_end_x - x));
	memset(__fb_buffer + y * __fb_pitch + (x << 2), color, len);
}

void tfb_draw_vline(int x, int y, int len, uint32_t color)
{
	int yend;

	if (y < 0) {
		len += y;
		y = 0;
	}

	x += __fb_off_x;
	y += __fb_off_y;

	if (len < 0 || x < __fb_off_x || x >= __fb_win_end_x)
		return;

	yend = MIN(y + len, __fb_win_end_y);

	volatile uint32_t *buf = ((volatile uint32_t *)__fb_buffer) + y * __fb_pitch_div4 + x;

	for (; y < yend; y++, buf += __fb_pitch_div4)
		*buf = color;
}

void tfb_fill_rect(int x, int y, int w, int h, uint32_t color)
{
	uint32_t yend;
	void *dest;

	if (w < 0) {
		x += w;
		w = -w;
	}

	if (h < 0) {
		y += h;
		h = -h;
	}

	x += __fb_off_x;
	y += __fb_off_y;

	if (x < 0) {
		w += x;
		x = 0;
	}

	if (y < 0) {
		h += y;
		y = 0;
	}

	if (w < 0 || h < 0)
		return;

	/*
	for (uint32_t cy = y; cy < yend; cy++) {
		//memset(dest, color, w);
		for (uint32_t cx = x; cx < x + w; cx++)
			tfb_draw_pixel(cx, cy, color);
	}
	*/

	w = MIN(w, MAX(0, (int)__fb_win_end_x - x));
	yend = MIN(y + h, __fb_win_end_y);

	dest = __fb_buffer + y * __fb_pitch + (x * 4);

	/* drm alignment weirdness */
	//if (drm)
	w *= 4;

	for (uint32_t cy = y; cy < yend; cy++, dest += __fb_pitch)
		memset(dest, color, w);
}

void tfb_draw_rect(int x, int y, int w, int h, uint32_t color)
{
	tfb_draw_hline(x, y, w, color);
	tfb_draw_vline(x, y, h, color);
	tfb_draw_vline(x + w - 1, y, h, color);
	tfb_draw_hline(x, y + h - 1, w, color);
}

static void midpoint_line(int x, int y, int x1, int y1, uint32_t color, bool swap_xy)
{
	const int dx = INT_ABS(x1 - x);
	const int dy = INT_ABS(y1 - y);
	const int sx = x1 > x ? 1 : -1;
	const int sy = y1 > y ? 1 : -1;
	const int incE = dy << 1;
	const int incNE = (dy - dx) << 1;
	const int inc_d[2] = { incNE, incE };
	const int inc_y[2] = { sy, 0 };

	int d = (dy << 1) - dx;

	if (swap_xy) {
		tfb_draw_pixel(y, x, color);

		while (x != x1) {
			x += sx;
			y += inc_y[d <= 0];
			d += inc_d[d <= 0];
			tfb_draw_pixel(y, x, color);
		}

	} else {
		tfb_draw_pixel(x, y, color);

		while (x != x1) {
			x += sx;
			y += inc_y[d <= 0];
			d += inc_d[d <= 0];
			tfb_draw_pixel(x, y, color);
		}
	}
}

void tfb_draw_line(int x0, int y0, int x1, int y1, uint32_t color)
{
	if (INT_ABS(y1 - y0) <= INT_ABS(x1 - x0))
		midpoint_line(x0, y0, x1, y1, color, false);
	else
		midpoint_line(y0, x0, y1, x1, color, true);
}

/*
 * Based on the pseudocode in:
 * https://sites.google.com/site/johnkennedyshome/home/downloadable-papers/bcircle.pdf
 *
 * Written by John Kennedy, Mathematics Department, Santa Monica College.
 */
void tfb_draw_circle(int cx, int cy, int r, uint32_t color)
{
	int x = r;
	int y = 0;
	int xch = 1 - 2 * r;
	int ych = 1;
	int rerr = 0;

	while (x >= y) {
		tfb_draw_pixel(cx + x, cy + y, color);
		tfb_draw_pixel(cx - x, cy + y, color);
		tfb_draw_pixel(cx - x, cy - y, color);
		tfb_draw_pixel(cx + x, cy - y, color);
		tfb_draw_pixel(cx + y, cy + x, color);
		tfb_draw_pixel(cx - y, cy + x, color);
		tfb_draw_pixel(cx - y, cy - x, color);
		tfb_draw_pixel(cx + y, cy - x, color);

		y++;
		rerr += ych;
		ych += 2;

		if (2 * rerr + xch > 0) {
			x--;
			rerr += xch;
			xch += 2;
		}
	}
}

/*
 * Simple algorithm for drawing a filled circle which just scans the whole
 * 2R x 2R square containing the circle.
 */
void tfb_fill_circle(int cx, int cy, int r, uint32_t color)
{
	const int r2 = r * r + r;

	for (int y = -r; y <= r; y++)
		for (int x = -r; x <= r; x++)
			if (x * x + y * y <= r2)
				tfb_draw_pixel(cx + x, cy + y, color);
}

#define DEBUGRENDER 0

/* x and y are expected to be relative to the screen rotation, however the buffer
 * width and height won't be, we need to handle rotating the buffer here.
 */
void blit_buf(unsigned char *buf, int x, int y, int w, int h, struct col bg, bool vflip)
{
	struct col prev_col = { .r = 0, .g = 0, .b = 0, .a = 0 };
	unsigned int col = tfb_make_color(bg.r, bg.g, bg.b);
	int tmp, rot = tfb_get_rotation();
	if (vflip)
		rot = (rot + 2) % 4;
	switch (rot) {
	case 1:
		tmp = w;
		w = h;
		h = tmp;
	case 0:
	default:
		break;
	}


	for (size_t i = 0; i < w; i++) {
		for (size_t j = 0; j < h; j++) {
#if DEBUGRENDER == 1
			if (i == 0 || i == w - 1 || j == 0 || j == h - 1) {
				tfb_draw_pixel(x + i, y + h - j, tfb_red);
				continue;
			}
#endif
			struct col rgba = *(struct col *)(buf + (j * w + i) * 4);
			if (rgba.a == 0 || rgba.rgba == bg.rgba)
				continue;

			// Alpha blending
			if (rgba.a != 255) {
				rgba.r = (rgba.r * rgba.a + bg.r * (255 - rgba.a)) >> 8;
				rgba.g = (rgba.g * rgba.a + bg.g * (255 - rgba.a)) >> 8;
				rgba.b = (rgba.b * rgba.a + bg.b * (255 - rgba.a)) >> 8;
			}

			// No need to generate the colour again if it's the same as the previous one
			if (rgba.rgba != prev_col.rgba) {
				prev_col.rgba = rgba.rgba;
				col = tfb_make_color(rgba.r, rgba.g, rgba.b);
			}

			if (vflip)
				tfb_draw_pixel(x + i, y + h - j, col);
			else
				tfb_draw_pixel(x + i, y + j, col);
		}
	}
}
