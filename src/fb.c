/* SPDX-License-Identifier: BSD-2-Clause */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <unistd.h>

#include "framebuffer.h"
#include "pbsplash.h"
#include "tfblib.h"

#define DEFAULT_FB_DEVICE "/dev/fb0"
#define DEFAULT_TTY_DEVICE "/dev/tty"

struct fb_var_screeninfo __fbi;

int __tfb_ttyfd = -1;

static int fbfd = -1;
static int drmfd = -1;

static void tfb_init_colors(void);

static int tfb_set_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t xoffset, uint32_t yoffset)
{
	if (x + w > (uint32_t)__fb_screen_w) {
		fprintf(stderr, "tfb_set_window: window exceeds screen width\n");
	}

	if (y + h > (uint32_t)__fb_screen_h) {
		fprintf(stderr, "tfb_set_window: window exceeds screen height\n");
	}

	__fb_off_x = xoffset + x;
	__fb_off_y = yoffset + y;
	__fb_win_w = w;
	__fb_win_h = h;
	__fb_win_end_x = __fb_off_x + __fb_win_w;
	__fb_win_end_y = __fb_off_y + __fb_win_h;

	return 0;
}

int tfb_acquire_drm(uint32_t flags, const char *device)
{
	int ret;
	ret = drm_framebuffer_init(&drmfd, device);
	if (ret) {
		fprintf(stderr, "Failed to get framebuffer\n");
		return ret;
	}

	__fb_real_buffer = drm->bufs[0].map;
	__fb_buffer = drm->bufs[1].map;
	__fb_pitch = drm->bufs[0].pitch;
	__fb_size = drm->bufs[0].size;
	__fb_pitch_div4 = __fb_pitch >> 2;

	__fb_screen_w = drm->bufs[0].width;
	__fb_screen_h = drm->bufs[0].height;

	__fb_r_pos = 16;
	__fb_r_mask_size = 8;
	__fb_r_mask = 0xff << __fb_r_pos;

	__fb_g_pos = 8;
	__fb_g_mask_size = 8;
	__fb_g_mask = 0xff << __fb_g_pos;

	__fb_b_pos = 0;
	__fb_b_mask_size = 8;
	__fb_b_mask = 0xff << __fb_b_pos;

	tfb_set_window(0, 0, __fb_screen_w, __fb_screen_h, 0, 0);
	tfb_init_colors();

	return 0;
}

int tfb_acquire_fb(uint32_t flags, const char *fb_device, const char *tty_device)
{
	static struct fb_fix_screeninfo fb_fixinfo;

	if (!fb_device)
		fb_device = DEFAULT_FB_DEVICE;

	if (!tty_device)
		tty_device = DEFAULT_TTY_DEVICE;

	fbfd = open(fb_device, O_RDWR);

	if (fbfd < 0) {
		perror("Couldn't open framebuffer device");
		return -1;
	}

	if (ioctl(fbfd, FBIOGET_FSCREENINFO, &fb_fixinfo) != 0) {
		perror("Couldn't get fb fixed info");
		close(fbfd);
		return -1;
	}

	if (ioctl(fbfd, FBIOGET_VSCREENINFO, &__fbi) != 0) {
		perror("Couldn't get fb vscreen info");
		close(fbfd);
		return -1;
	}

	__fb_pitch = fb_fixinfo.line_length;
	__fb_size = __fb_pitch * __fbi.yres;
	__fb_pitch_div4 = __fb_pitch >> 2;

	if (__fbi.bits_per_pixel != 32) {
		fprintf(stderr, "Unsupported framebuffer format: %u\n", __fbi.bits_per_pixel);
		close(fbfd);
		return -1;
	}

	if (__fbi.red.msb_right || __fbi.green.msb_right || __fbi.blue.msb_right) {
		fprintf(stderr, "Sanity check failed for RGB masks: %u %u %u\n",
			__fbi.red.msb_right, __fbi.green.msb_right, __fbi.blue.msb_right);
		close(fbfd);
		return -1;
	}

	__tfb_ttyfd = open(tty_device, O_RDWR);

	if (__tfb_ttyfd < 0) {
		perror("Couldn't open tty device");
		close(fbfd);
		return -1;
	}

	if (!(flags & TFB_FL_NO_TTY_KD_GRAPHICS)) {
		if (ioctl(__tfb_ttyfd, KDSETMODE, KD_GRAPHICS) != 0) {
			perror("Couldn't set tty to graphics mode");
			close(fbfd);
			ioctl(__tfb_ttyfd, KDSETMODE, KD_TEXT);
			close(__tfb_ttyfd);
			return -1;
		}
	}

	__fb_real_buffer = mmap(NULL, __fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
	if (__fb_real_buffer == MAP_FAILED) {
		perror("Couldn't mmap framebuffer");
		close(fbfd);
		close(__tfb_ttyfd);
		return -1;
	}

	if (flags & TFB_FL_USE_DOUBLE_BUFFER) {
		__fb_buffer = malloc(__fb_size);
		if (!__fb_buffer) {
			perror("Couldn't allocate double buffer");
			tfb_release_fb();
			return -1;
		}
	} else {
		__fb_buffer = __fb_real_buffer;
	}

	__fb_screen_w = __fbi.xres;
	__fb_screen_h = __fbi.yres;

	__fb_r_pos = __fbi.red.offset;
	__fb_r_mask_size = __fbi.red.length;
	__fb_r_mask = ((1 << __fb_r_mask_size) - 1) << __fb_r_pos;

	__fb_g_pos = __fbi.green.offset;
	__fb_g_mask_size = __fbi.green.length;
	__fb_g_mask = ((1 << __fb_g_mask_size) - 1) << __fb_g_pos;

	__fb_b_pos = __fbi.blue.offset;
	__fb_b_mask_size = __fbi.blue.length;
	__fb_b_mask = ((1 << __fb_b_mask_size) - 1) << __fb_b_pos;

	tfb_set_window(0, 0, __fb_screen_w, __fb_screen_h, __fbi.xoffset, __fbi.yoffset);
	tfb_init_colors();

	return 0;
}

void tfb_release_fb(void)
{
	if (drmfd >= 0) {
		drm_framebuffer_close(drmfd);
		return;
	}
	if (__fb_real_buffer)
		munmap(__fb_real_buffer, __fb_size);

	if (__fb_buffer != __fb_real_buffer)
		free(__fb_buffer);

	if (__tfb_ttyfd != -1) {
		ioctl(__tfb_ttyfd, KDSETMODE, KD_TEXT);
		close(__tfb_ttyfd);
	}

	if (fbfd != -1)
		close(fbfd);
}

int tfb_get_rotation(void)
{
	return __fbi.rotate;
}

void tfb_flush_rect(int x, int y, int w, int h)
{
	int yend;

	if (__fb_buffer == __fb_real_buffer)
		return;

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

	w = MIN(w, MAX(0, __fb_win_end_x - x));
	yend = MIN(y + h, __fb_win_end_y);

	size_t offset = y * __fb_pitch + (__fb_off_x << 2);
	void *dest = __fb_real_buffer + offset;
	void *src = __fb_buffer + offset;
	uint32_t rect_pitch = w << 2;

	for (int cy = y; cy < yend; cy++, src += __fb_pitch, dest += __fb_pitch)
		memcpy(dest, src, rect_pitch);
}

void tfb_flush_window(void)
{
	tfb_flush_rect(0, 0, __fb_win_w, __fb_win_h);
}

int tfb_flush_fb(void)
{
	int ret;
	struct modeset_buf *buf;
	if (drmfd >= 0) {
		//printf("%s buffer %d\n", __func__, drm->front_buf);
		buf = &drm->bufs[drm->front_buf ^ 1];
		ret = drmModeSetCrtc(drmfd, drm->crtc, buf->fb, 0, 0,
					&drm->conn, 1, &drm->mode);
		if (ret)
			fprintf(stderr, "cannot flip CRTC for connector %u (%d): %m\n",
				drm->conn, errno);
		else
			drm->front_buf ^= 1;

		/* Swap the tfblib copies of the pointers */
		__fb_buffer = buf->map; //drm->bufs[drm->front_buf ^ 1].map;
		return 0;
	}
	__fbi.activate |= FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
	if (ioctl(fbfd, FBIOPUT_VSCREENINFO, &__fbi) < 0) {
		perror("Couldn't flush framebuffer");
		return -1;
	}

	return 0;
}

uint32_t tfb_screen_width_mm(void)
{
	if (drmfd >= 0)
		return drm->mm_width;

	return __fbi.width;
}
uint32_t tfb_screen_height_mm(void)
{
	if (drmfd >= 0)
		return drm->mm_height;

	return __fbi.height;
}

/*
 * ----------------------------------------------------------------------------
 *
 * Colors
 *
 * ----------------------------------------------------------------------------
 */

uint32_t tfb_red;
uint32_t tfb_green;
uint32_t tfb_blue;
uint32_t tfb_white;
uint32_t tfb_gray;
uint32_t tfb_black;

static void tfb_init_colors(void)
{
	tfb_red = tfb_make_color(255, 0, 0);
	tfb_green = tfb_make_color(0, 255, 0);
	tfb_blue = tfb_make_color(0, 0, 255);
	tfb_white = tfb_make_color(255, 255, 255);
	tfb_gray = tfb_make_color(128, 128, 128);
	tfb_black = tfb_make_color(0, 0, 0);
}
