/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file tfblib.h
 * @brief Tfblib's main header file
 */

#pragma once
#define _TFBLIB_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "config.h"
#include "pbsplash.h"

/// Convenience macro used to shorten the signatures. Undefined at the end.
#define u8 uint8_t

/// Convenience macro used to shorten the signatures. Undefined at the end.
#define u32 uint32_t

/*
 * ----------------------------------------------------------------------------
 *
 * Initialization/setup functions and definitions
 *
 * ----------------------------------------------------------------------------
 */

/**
 * \addtogroup flags Flags
 * @{
 */

/**
 * Do NOT put TTY in graphics mode.
 *
 * Passing this flag to tfb_acquire_fb() will
 * allow to use the framebuffer and to see stdout on TTY as well. That usually
 * is undesirable because the text written to TTY will overwrite the graphics.
 */
#define TFB_FL_NO_TTY_KD_GRAPHICS (1 << 0)

/**
 * Do NOT write directly onto the framebuffer.
 *
 * Passing this flag to tfb_acquire_fb() will make it allocate a regular memory
 * buffer where all the writes (while drawing) will be directed to. The changes
 * will appear on-screen only after manually called tfb_flush_rect() or
 * tfb_flush_rect(). This flag is useful for applications needing to clean and
 * redraw the whole screen (or part of it) very often (e.g. games) in order to
 * avoid the annoying flicker effect.
 */
#define TFB_FL_USE_DOUBLE_BUFFER (1 << 1)

/** @} */

/**
 * Opens and maps the framebuffer device in the current address space
 *
 * A successful call to tfb_acquire_fb() is mandatory before calling any drawing
 * functions, including the tfb_clear_* and tfb_flush_* functions.
 *
 * @param[in] flags        One or more among: #TFB_FL_NO_TTY_KD_GRAPHICS,
 *                         #TFB_FL_USE_DOUBLE_BUFFER.
 *
 * @param[in] fb_device    The framebuffer device file. Can be NULL.
 *                         Defaults to /dev/fb0.
 *
 * @param[in] tty_device   The tty device file to use for setting tty in
 *                         graphics mode. Can be NULL. Defaults to /dev/tty.
 *
 * @return                 #TFB_SUCCESS in case of success or one of the
 *                         following errors:
 *                             #TFB_ERR_OPEN_FB,
 *                             #TFB_ERR_IOCTL_FB,
 *                             #TFB_ERR_UNSUPPORTED_VIDEO_MODE,
 *                             #TFB_ERR_TTY_GRAPHIC_MODE,
 *                             #TFB_ERR_MMAP_FB,
 *                             #TFB_ERR_OUT_OF_MEMORY.
 *
 * \note This function does not affect the kb mode. tfb_set_kb_raw_mode() can
 *       be called before or after tfb_acquire_fb().
 */
int tfb_acquire_fb(u32 flags, const char *fb_device, const char *tty_device);

#ifdef CONFIG_DRM_SUPPORT
int tfb_acquire_drm(uint32_t flags, const char *device);
#else
#define tfb_acquire_drm(f, d) ({ -1; })
#endif

/**
 * Release the framebuffer device
 *
 * \note    The function **must** be called before exiting, otherwise the TTY
 *          will remain in graphics mode and be unusable.
 *
 * \note    This function does not affect the kb mode. If tfb_set_kb_raw_mode()
 *          has been used, tfb_restore_kb_mode() must be called to restore the
 *          kb mode to its original value.
 */
void tfb_release_fb(void);

/**
 * Limit the drawing to a window having size (w, h) at the center of the screen
 *
 * tfb_set_center_window_size() is a wrapper of tfb_set_window() which just
 * calculates the (x, y) coordinates of the window in order it to be at the
 * center of the screen.
 *
 * @param[in] w      Width of the window, in pixels
 * @param[in] h      Height of the window, in pixels
 *
 * @return           #TFB_SUCCESS in case of success or #TFB_ERR_INVALID_WINDOW.
 */
int tfb_set_center_window_size(u32 w, u32 h);

/*
 * ----------------------------------------------------------------------------
 *
 * Text-related functions and definitions
 *
 * ----------------------------------------------------------------------------
 */

/**
 * \addtogroup flags Flags
 * @{
 */

/**
 * When passed to the 'w' param of tfb_set_font_by_size(), means that any font
 * width is acceptable.
 */
#define TFB_FONT_ANY_WIDTH 0

/**
 * When passed to the 'h' param of tfb_set_font_by_size(), means that any font
 * height is acceptable.
 */
#define TFB_FONT_ANY_HEIGHT 0

/** @} */

/**
 * Opaque font type
 */
typedef void *tfb_font_t;

/**
 * Font info structure
 *
 * tfb_iterate_over_fonts() passes a pointer to a struct tfb_font_info * for
 * each statically embedded font in the library to the callback function.
 */
struct tfb_font_info {

	const char *name; /**< Font's file name */
	u32 width; /**< Font's character width in pixels */
	u32 height; /**< Font's character height in pixels */
	u32 psf_version; /**< PSF version: either 1 or 2 */
	tfb_font_t font_id; /**< An opaque identifier of the font */
};

/**
 * Callback type accepted by tfb_iterate_over_fonts().
 */
typedef bool (*tfb_font_iter_func)(struct tfb_font_info *cb, void *user_arg);

/**
 * Iterate over the fonts embedded in the library.
 *
 * tfb_iterate_over_fonts() calls 'cb' once for each embedded font passing to
 * it a pointer a struct tfb_font_info structure and the user_arg until either
 * the font list is over or the callback returned false.
 *
 * @param[in] cb           An user callback function
 * @param[in] user_arg     An arbitrary user pointer that will be passed to the
 *                         callback function.
 */
void tfb_iterate_over_fonts(tfb_font_iter_func cb, void *user_arg);

/**
 * Set the font used by the functions for drawing text
 *
 * @param[in] font_id      An opaque identifier provided by the library either
 *                         as a member of struct tfb_font_info, or returned as
 *                         an out parameter by tfb_dyn_load_font().
 *
 * @return                 #TFB_SUCCESS in case of success or
 *                         #TFB_ERR_INVALID_FONT_ID otherwise.
 */
int tfb_set_current_font(tfb_font_t font_id);

/**
 * Load dynamically a PSF font file
 *
 * @param[in]     file     File path
 * @param[in,out] font_id  Address of a tfb_font_t variable that will
 *                         be set by the function in case of success.
 *
 * @return                 #TFB_SUCCESS in case of success or one of the
 *                         following errors:
 *                             #TFB_ERR_READ_FONT_FILE_FAILED,
 *                             #TFB_ERR_OUT_OF_MEMORY.
 */
int tfb_dyn_load_font(const char *file, tfb_font_t *font_id);

/**
 * Unload a dynamically-loaded font
 *
 * @param[in]     font_id  Opaque pointer returned by tfb_dyn_load_font()
 *
 * @return                 #TFB_SUCCESS in case of success or
 *                         #TFB_ERR_NOT_A_DYN_LOADED_FONT if the caller passed
 *                         to it the font_id of an embedded font.
 */
int tfb_dyn_unload_font(tfb_font_t font_id);

/**
 * Select the first font matching the given (w, h) criteria
 *
 * The tfb_set_font_by_size() function iterates over the fonts embedded in the
 * library and sets the first font having width = w and height = h.
 *
 * @param[in]     w        Desired width of the font.
 *                         The caller may pass #TFB_FONT_ANY_WIDTH to tell the
 *                         function that any font width is acceptable.
 *
 * @param[in]     h        Desired height of the font.
 *                         The caller may pass #TFB_FONT_ANY_HEIGHT to tell the
 *                         function that any font width is acceptable.
 *
 * @return        #TFB_SUCCESS in case a font matching the given criteria has
 *                been found or #TFB_ERR_FONT_NOT_FOUND otherwise.
 */
int tfb_set_font_by_size(int w, int h);

/**
 * Get current font's width
 *
 * @return        the width (in pixels) of the current font or 0 in case there
 *                is no currently selected font.
 */
int tfb_get_curr_font_width(void);

/**
 * Get current font's height
 *
 * @return        the height (in pixels) of the current font or 0 in case there
 *                is no currently selected font.
 */
int tfb_get_curr_font_height(void);

/*
 * ----------------------------------------------------------------------------
 *
 * Drawing functions
 *
 * ----------------------------------------------------------------------------
 */

/**
 * Value for 1 degree (of 360) of hue, when passed to tfb_make_color_hsv()
 */
#define TFB_HUE_DEGREE 256

/**
 * Get a representation of the RGB color (r, g, b) for the current video mode
 *
 * @param[in]  r        Red color component [0, 255]
 * @param[in]  g        Green color component [0, 255]
 * @param[in]  b        Blue color component [0, 255]
 *
 * @return              A framebuffer-specific representation of the RGB color
 *                      passed using the r, g, b parameters.
 */
inline u32 tfb_make_color(u8 r, u8 g, u8 b);

/**
 * Get a representation of the HSV color (h, s, v) for the current video mode
 *
 * @param[in]  h        Hue                  [0, 360 * #TFB_HUE_DEGREE]
 * @param[in]  s        Saturation           [0, 255]
 * @param[in]  v        Value (Brightness)   [0, 255]
 *
 * @return              A framebuffer-specific representation of the HSV color
 *                      passed using the h, s, v parameters.
 *
 * \note    1 degree of hue is #TFB_HUE_DEGREE, not simply 1. This is necessary
 *          in order to increase the precision of the internal integer-only
 *          computations.
 */

u32 tfb_make_color_hsv(u32 h, u8 s, u8 v);

/**
 * Set the color of the pixel at (x, y) to 'color'
 *
 * @param[in]  x        Window-relative X coordinate of the pixel
 * @param[in]  y        Window-relative Y coordinate of the pixel
 * @param[in]  color    A color returned by tfb_make_color()
 *
 * \note By default, the library uses as "window" the whole screen, therefore
 *       by default the point (x, y) corresponds to the pixel at (x, y) on the
 *       screen. But, after calling tfb_set_window() the origin of the
 *       coordinate system gets shifted.
 */
inline void tfb_draw_pixel(int x, int y, u32 color);

/**
 * Draw a horizonal line on-screen
 *
 * @param[in]  x        Window-relative X coordinate of line's first point
 * @param[in]  y        Window-relative Y coordinate of line's first point
 * @param[in]  len      Length of the line, in pixels
 * @param[in]  color    Color of the line. See tfb_make_color().
 *
 * Calling tfb_draw_hline(x, y, len, color) is equivalent to calling:
 *       tfb_draw_line(x, y, x + len, y, color)
 *
 * The only difference between the two functions is in the implementation: given
 * the simpler task of tfb_draw_hline(), it can be implemented in much more
 * efficient way.
 */
void tfb_draw_hline(int x, int y, int len, u32 color);

/**
 * Draw a vertical line on-screen
 *
 * @param[in]  x        Window-relative X coordinate of line's first point
 * @param[in]  y        Window-relative Y coordinate of line's first point
 * @param[in]  len      Length of the line, in pixels
 * @param[in]  color    Color of the line. See tfb_make_color().
 *
 * Calling tfb_draw_vline(x, y, len, color) is equivalent to calling:
 *       tfb_draw_line(x, y, x, y + len, color)
 *
 * The only difference between the two functions is in the implementation: given
 * the simpler task of tfb_draw_vline(), it can be implemented in much more
 * efficient way.
 */
void tfb_draw_vline(int x, int y, int len, u32 color);

/**
 * Draw a line on-screen
 *
 * @param[in]  x0       Window-relative X coordinate of line's first point
 * @param[in]  y0       Window-relative Y coordinate of line's first point
 * @param[in]  x1       Window-relative X coordinate of line's second point
 * @param[in]  y1       Window-relative Y coordinate of line's second point
 * @param[in]  color    Color of the line. See tfb_make_color().
 */
void tfb_draw_line(int x0, int y0, int x1, int y1, u32 color);

/**
 * Draw an empty rectangle on-screen
 *
 * @param[in]  x        Window-relative X coordinate of rect's top-left corner
 * @param[in]  y        Window-relative Y coordinate of rect's top-left corner
 * @param[in]  w        Width of the rectangle
 * @param[in]  h        Height of the rectangle
 * @param[in]  color    Color of the rectangle
 */
void tfb_draw_rect(int x, int y, int w, int h, u32 color);

/**
 * Draw filled rectangle on-screen
 *
 * @param[in]  x        Window-relative X coordinate of rect's top-left corner
 * @param[in]  y        Window-relative Y coordinate of rect's top-left corner
 * @param[in]  w        Width of the rectangle
 * @param[in]  h        Height of the rectangle
 * @param[in]  color    Color of the rectangle
 */
void tfb_fill_rect(int x, int y, int w, int h, u32 color);

/**
 * Draw an empty circle on-screen
 *
 * @param[in]  cx       X coordinate of circle's center
 * @param[in]  cy       Y coordinate of circle's center
 * @param[in]  r        Circle's radius
 * @param[in]  color    Circle's color
 */
void tfb_draw_circle(int cx, int cy, int r, u32 color);

/**
 * Draw a filled circle on-screen
 *
 * @param[in]  cx       X coordinate of circle's center
 * @param[in]  cy       Y coordinate of circle's center
 * @param[in]  r        Circle's radius
 * @param[in]  color    Circle's color
 */
void tfb_fill_circle(int cx, int cy, int r, u32 color);

/**
 * Blit a 32-bit RGBA buffer to the screen at the specified coordinates.
 *
 * @param[in]  buf      The buffer
 * @param[in]  x        Window-relative X coordinate of the top-left corner of
 *                      the buffer
 * @param[in]  y        Window-relative Y coordinate of the top-left corner of
 *                      the buffer
 * @param[in]  w        Width of the buffer
 * @param[in]  h        Height of the buffer
 * @param[in]  bg       Background color. Pixels with this color are not
 *                      blitted to the screen.
 * @param[in]  vflip    If true, the buffer is flipped vertically
 */
void blit_buf(unsigned char *buf, int x, int y, int w, int h, struct col bg, bool vflip);

/**
 * Set all the pixels of the screen to the supplied color
 *
 * @param[in]  color    The color. See tfb_make_color().
 */
void tfb_clear_screen(u32 color);

/**
 * Set all the pixels of the current window to the supplied color
 *
 * @param[in]  color    The color. See tfb_make_color().
 *
 * \note Unless tfb_set_window() has been called, the current window is by
 *       default large as the whole screen.
 */
void tfb_clear_win(u32 color);

/**
 * Get screen's physical width in mm
 *
 * @return  the width of the screen in mm
 */
u32 tfb_screen_width_mm(void);

/**
 * Get screen's physical height in mm
 *
 * @return  the height of the screen in mm
 */
u32 tfb_screen_height_mm(void);

/**
 * Get the screen's rotation as a multiple of 90 degrees
 * 1: 90 degrees
 * 2: 180 degrees
 * 3: 270 degrees
 */
int tfb_get_rotation(void);

/**
 * Flush a given region to the actual framebuffer
 *
 * @param[in]  x        Window-relative X coordinate of region's position
 * @param[in]  y        Window-relative Y coordinate of region's position
 * @param[in]  w        Width of the region (in pixels)
 * @param[in]  h        Height of the region (in pixels)
 *
 * In case tfb_acquire_fb() has been called with #TFB_FL_USE_DOUBLE_BUFFER,
 * this function copies the pixels in the specified region to actual
 * framebuffer. By default double buffering is not used and this function has no
 * effect.
 */
void tfb_flush_rect(int x, int y, int w, int h);

/**
 * Flush the current window to the actual framebuffer
 *
 * A shortcut for tfb_flush_rect(0, 0, tfb_win_width(), tfb_win_height()).
 *
 * @see tfb_flush_rect
 * @see tfb_set_window
 */
void tfb_flush_window(void);

/**
 * Flush the framebuffer, causing it to update. This is different
 * to tfb_flush_window() as it doesn't deal with double_buffering,
 * rather it handles the case where the framebuffer has to be "ACTIVATED".
 *
 * @return #TFB_SUCCESS on success or #TFB_ERR_FB_FLUSH_IOCTL_FAILED
 *    on failure.
 *
 * @see tfb_flush_window
 */
int tfb_flush_fb(void);

/* Essential variables */
extern void *__fb_buffer;
extern void *__fb_real_buffer;
extern int __fb_screen_w;
extern int __fb_screen_h;
extern size_t __fb_size;
extern size_t __fb_pitch;
extern size_t __fb_pitch_div4; /* see the comment in drawing.c */

/* Window-related variables */
extern int __fb_win_w;
extern int __fb_win_h;
extern int __fb_off_x;
extern int __fb_off_y;
extern int __fb_win_end_x;
extern int __fb_win_end_y;

/* Color-related variables */
extern u32 __fb_r_mask;
extern u32 __fb_g_mask;
extern u32 __fb_b_mask;
extern u8 __fb_r_mask_size;
extern u8 __fb_g_mask_size;
extern u8 __fb_b_mask_size;
extern u8 __fb_r_pos;
extern u8 __fb_g_pos;
extern u8 __fb_b_pos;

extern uint32_t tfb_red;
extern uint32_t tfb_green;
extern uint32_t tfb_blue;
extern uint32_t tfb_white;
extern uint32_t tfb_gray;
extern uint32_t tfb_black;

inline u32 tfb_make_color(u8 r, u8 g, u8 b)
{
	return ((r << __fb_r_pos) & __fb_r_mask) | ((g << __fb_g_pos) & __fb_g_mask) |
	       ((b << __fb_b_pos) & __fb_b_mask);
}

inline void tfb_draw_pixel(int x, int y, u32 color)
{
	x += __fb_off_x;
	y += __fb_off_y;

	if ((u32)x < (u32)__fb_win_end_x && (u32)y < (u32)__fb_win_end_y)
		((volatile u32 *)__fb_buffer)[x + y * __fb_pitch_div4] = color;
}

inline u32 tfb_screen_width(void)
{
	return __fb_screen_w;
}
inline u32 tfb_screen_height(void)
{
	return __fb_screen_h;
}
inline u32 tfb_win_width(void)
{
	return __fb_win_w;
}
inline u32 tfb_win_height(void)
{
	return __fb_win_h;
}

/* undef the the convenience types defined above */
#undef u8
#undef u32
