#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <math.h>
#include <string.h>
#define NANOSVG_ALL_COLOR_KEYWORDS // Include full list of color keywords.
#include "nanosvg.h"
#include "nanosvgrast.h"
#include "pbsplash.h"
#include "tfblib.h"
#include "timespec.h"

#define MSG_MAX_LEN 4096
#define DEFAULT_FONT_PATH "/usr/share/pbsplash/OpenSans-Regular.svg"
#define LOGO_SIZE_MAX_MM 45
#define FONT_SIZE_PT 9
#define FONT_SIZE_B_PT 6
#define B_MESSAGE_OFFSET_MM 3
#define PT_TO_MM 0.38f
#define TTY_PATH_LEN 11

volatile sig_atomic_t terminate = 0;

bool debug = true;
struct col bg = { .r = 0, .g = 0, .b = 0, .a = 255 };

static int screenWidth, screenHeight;

#define zalloc(size) calloc(1, size)

#define LOG(fmt, ...)                               \
	do {                                        \
		if (debug)                          \
			printf(fmt, ##__VA_ARGS__); \
	} while (0)

static int usage()
{
	// clang-format off
	fprintf(stderr, "pbsplash: postmarketOS bootsplash generator\n");
	fprintf(stderr, "-------------------------------------------\n");
	fprintf(stderr, "pbsplash [-v] [-h] [-f font] [-s splash image] [-m message]\n");
	fprintf(stderr, "         [-b message bottom] [-o font size bottom]\n");
	fprintf(stderr, "         [-p font size] [-q max logo size] [-d] [-e]\n\n");
	fprintf(stderr, "    -v           enable verbose logging\n");
	fprintf(stderr, "    -h           show this help\n");
	fprintf(stderr, "    -f           path to SVG font file (default: %s)\n", DEFAULT_FONT_PATH);
	fprintf(stderr, "    -s           path to splash image to display\n");
	fprintf(stderr, "    -m           message to show under the splash image\n");
	fprintf(stderr, "    -b           message to show at the bottom\n");
	fprintf(stderr, "    -o           font size bottom in pt (default: %d)\n", FONT_SIZE_B_PT);
	fprintf(stderr, "    -p           font size in pt (default: %d)\n", FONT_SIZE_PT);
	fprintf(stderr, "    -q           max logo size in mm (default: %d)\n", LOGO_SIZE_MAX_MM);
	fprintf(stderr, "    -d           custom DPI (for testing)\n");
	fprintf(stderr, "    -e           error (no loading animation)\n");
	// clang-format on

	return 1;
}

static void term(int signum)
{
	terminate = 1;
}

static void draw_svg(NSVGimage *image, int x, int y, int w, int h)
{
	float sz = (int)((float)w / (float)image->width * 100.f) / 100.f;
	LOG("draw_svg: (%d, %d), %dx%d, %f\n", x, y, w, h, sz);
	NSVGrasterizer *rast = nsvgCreateRasterizer();
	unsigned char *img = zalloc(w * h * 4);
	struct col bg = { .r = 0, .g = 0, .b = 0, .a = 255 };
	nsvgRasterize(rast, image, 0, 0, sz, img, w, h, w * 4);

	blit_buf(img, x, y, w, h, bg, false);

	free(img);
	nsvgDeleteRasterizer(rast);
}

static void draw_text(const NSVGimage *font, const char *text, int x, int y, int width, int height,
		      float scale, unsigned int tfb_col)
{
	LOG("text '%s': fontsz=%f, x=%d, y=%d, dimensions: %d x %d\n", text, scale, x, y, width,
	    height);
	NSVGshape **shapes = nsvgGetTextShapes(font, text, strlen(text));
	unsigned char *img = zalloc(width * height * 4);
	NSVGrasterizer *rast = nsvgCreateRasterizer();
	struct col bg = { .r = 0, .g = 0, .b = 0, .a = 255 };

	nsvgRasterizeText(rast, font, 0, 0, scale, img, width, height, width * 4, text);

	blit_buf(img, x, y, width, height, bg, true);

	free(img);
	free(shapes);
	nsvgDeleteRasterizer(rast);
}

static inline float getShapeWidth(const NSVGimage *font, const NSVGshape *shape)
{
	if (shape) {
		return shape->horizAdvX;
	} else {
		return font->defaultHorizAdv;
	}
}

/*
 * Get the dimensions of a string in pixels.
 * based on the font size and the font SVG file.
 */
static const char *getTextDimensions(const NSVGimage *font, const char *text, float scale,
				     int *width, int *height)
{
	int i, j;
	int fontHeight = (font->fontAscent - font->fontDescent) * scale;
	int maxWidth = 0;

	if (text == NULL)
		return text;

	// Pre-allocate 3x the size to account for any word splitting
	char *out_text = zalloc(strlen(text) * 3 + 1);

	*width = 2; // font->defaultHorizAdv * scale;
	// The height is simply the height of the font * the scale factor
	*height = fontHeight;

	NSVGshape **shapes = nsvgGetTextShapes(font, text, strlen(text));
	bool line_has_space = false;
	// Iterate over every glyph in the string to get the total width
	// and handle line-splitting
	for (i = 0, j = 0; text[i] != '\0'; i++, j++) {
		NSVGshape *shape = shapes[i];
		out_text[j] = text[i];
		if (*width > screenWidth * 0.95) {
			if (!line_has_space) {
				i--;
				if (i < 1) {
					fprintf(stderr,
						"ERROR: Text is too long to fit on screen!");
					goto out;
				}
			} else {
				int old_j = j;
				while (out_text[j] != ' ' && j > 0) {
					j--;
				}
				i = i - (old_j - j);
				if (i <= 0) {
					line_has_space = false;
					fprintf(stderr,
						"ERROR: Text is too long to fit on screen!");
					goto out;
				}
			}
			out_text[j] = '\n';
		}

		if (out_text[j] == '\n') {
			line_has_space = false;
			*height += fontHeight;
			maxWidth = *width > maxWidth ? *width : maxWidth;
			*width = 0;
			continue;
		} else if (text[i] == ' ') {
			line_has_space = true;
		}

		*width += round(getShapeWidth(font, shape) * scale);
	}

	*width = *width > maxWidth ? *width : maxWidth;

out:
	free(shapes);
	return out_text;
}

struct dpi_info {
	long dpi;
	int pixels_per_milli;
	float logo_size_px;
	int logo_size_max_mm;
};

static void calculate_dpi_info(struct dpi_info *dpi_info)
{
	int w_mm = tfb_screen_width_mm();
	int h_mm = tfb_screen_height_mm();

	if ((w_mm < 1 || h_mm < 1) && !dpi_info->dpi) {
		fprintf(stderr, "ERROR!!!: Invalid screen size: %dmmx%dmm\n", w_mm, h_mm);

		// Assume a dpi of 300
		// This should be readable everywhere
		// Except ridiculous HiDPI displays
		// which honestly should expose their physical
		// dimensions....
		dpi_info->dpi = 300;
	}

	// If DPI is specified on cmdline then calculate display size from it
	// otherwise calculate the dpi based on the display size.
	if (dpi_info->dpi > 0) {
		w_mm = screenWidth / (float)dpi_info->dpi * 25.4;
		h_mm = screenHeight / (float)dpi_info->dpi * 25.4;
	} else {
		dpi_info->dpi = (float)screenWidth / (float)w_mm * 25.4;
	}
	dpi_info->pixels_per_milli = (float)screenWidth / (float)w_mm;

	if (dpi_info->logo_size_max_mm * dpi_info->pixels_per_milli > screenWidth)
		dpi_info->logo_size_max_mm = (screenWidth * 0.75f) / dpi_info->pixels_per_milli;

	dpi_info->logo_size_px =
		(float)(screenWidth < screenHeight ? screenWidth : screenHeight) * 0.75f;
	if (w_mm > 0 && h_mm > 0) {
		if (w_mm < h_mm) {
			if (w_mm > dpi_info->logo_size_max_mm * 1.2f)
				dpi_info->logo_size_px =
					dpi_info->logo_size_max_mm * dpi_info->pixels_per_milli;
		} else {
			if (h_mm > dpi_info->logo_size_max_mm * 1.2f)
				dpi_info->logo_size_px =
					dpi_info->logo_size_max_mm * dpi_info->pixels_per_milli;
		}
	}

	printf("%dx%d @ %dx%dmm, dpi=%ld, logo_size_px=%f\n", screenWidth, screenHeight, w_mm, h_mm,
	       dpi_info->dpi, dpi_info->logo_size_px);
}

struct msg_info {
	const char *src_message;
	const char *message;
	int x;
	int y;
	int width;
	int height;
	float fontsz;
};

static void load_message(struct msg_info *msg_info, const struct dpi_info *dpi_info,
			 float font_size_pt, const NSVGimage *font)
{
	msg_info->fontsz = (font_size_pt * PT_TO_MM) / (font->fontAscent - font->fontDescent) *
			   dpi_info->pixels_per_milli;
	msg_info->message = getTextDimensions(font, msg_info->src_message, msg_info->fontsz,
					      &msg_info->width, &msg_info->height);
	msg_info->x = (screenWidth - msg_info->width) / 2;
	// Y coordinate is set later
}

static void free_message(struct msg_info *msg_info)
{
	if (!msg_info)
		return;
	if (msg_info->message && msg_info->message != msg_info->src_message)
		free((void *)msg_info->message);
}

struct messages {
	const char *font_path;
	NSVGimage *font;
	int font_size_pt;
	int font_size_b_pt;
	struct msg_info *msg;
	struct msg_info *bottom_msg;
};

static inline void show_message(const struct msg_info *msg_info, const NSVGimage *font)
{
	draw_text(font, msg_info->message, msg_info->x, msg_info->y, msg_info->width,
		  msg_info->height, msg_info->fontsz, tfb_gray);
}

static void show_messages(struct messages *msgs, const struct dpi_info *dpi_info)
{
	static bool font_failed = false;
	if (font_failed)
		return;

	if (!msgs->font)
		msgs->font = nsvgParseFromFile(msgs->font_path, "px", 512);
	if (!msgs->font || !msgs->font->shapes) {
		font_failed = true;
		fprintf(stderr, "failed to load SVG font, can't render messages\n");
		fprintf(stderr, "  font_path: %s\n", msgs->font_path);
		fprintf(stderr, "msg: %s\n\nbottom_message: %s\n", msgs->msg->src_message,
			msgs->bottom_msg->src_message);
		return;
	}

	if (msgs->bottom_msg) {
		if (!msgs->bottom_msg->message) {
			load_message(msgs->bottom_msg, dpi_info, msgs->font_size_b_pt, msgs->font);
			msgs->bottom_msg->y = screenHeight - msgs->bottom_msg->height -
					      MM_TO_PX(dpi_info->dpi, B_MESSAGE_OFFSET_MM);
		}
		show_message(msgs->bottom_msg, msgs->font);
	}

	if (msgs->msg) {
		if (!msgs->msg->message) {
			load_message(msgs->msg, dpi_info, msgs->font_size_pt, msgs->font);
			if (msgs->bottom_msg)
				msgs->msg->y =
					msgs->bottom_msg->y - msgs->msg->height -
					(MM_TO_PX(dpi_info->dpi, msgs->font_size_b_pt * PT_TO_MM) *
					 0.6);
			else
				msgs->msg->y =
					screenHeight - msgs->msg->height -
					(MM_TO_PX(dpi_info->dpi, msgs->font_size_pt * PT_TO_MM) *
					 2);
		}
		show_message(msgs->msg, msgs->font);
	}
}

struct image_info {
	const char *path;
	NSVGimage *image;
	float width;
	float height;
	float x;
	float y;
};

static int load_image(const struct dpi_info *dpi_info, struct image_info *image_info)
{
	int logo_size_px = dpi_info->logo_size_px;

	image_info->image = nsvgParseFromFile(image_info->path, "", logo_size_px);
	if (!image_info->image) {
		fprintf(stderr, "failed to load SVG image\n");
		fprintf(stderr, "  image path: %s\n", image_info->path);
		return 1;
	}

	// For taller images make sure they don't get too wide
	if (image_info->image->width < image_info->image->height * 1.1)
		logo_size_px = MM_TO_PX(dpi_info->dpi, 25);

	float sz = (float)logo_size_px / (image_info->image->width > image_info->image->height ?
						  image_info->image->height :
						  image_info->image->width);
	image_info->width = image_info->image->width * sz + 0.5;
	image_info->height = image_info->image->height * sz + 0.5;
	if (image_info->width > (dpi_info->logo_size_max_mm * dpi_info->pixels_per_milli)) {
		float scalefactor =
			((float)(dpi_info->logo_size_max_mm * dpi_info->pixels_per_milli) /
			 image_info->width);
		// printf("Got scale factor: %f\n", scalefactor);
		image_info->width = dpi_info->logo_size_max_mm * dpi_info->pixels_per_milli;
		image_info->height *= scalefactor;
	}
	image_info->x = (float)screenWidth / 2 - image_info->width * 0.5f;
	image_info->y = (float)screenHeight / 2 - image_info->height * 0.5f;

	return 0;
}

int main(int argc, char **argv)
{
	int rc = 0;
	char *message = NULL;
	char *message_bottom = NULL;
	char active_tty[TTY_PATH_LEN + 1];
	struct sigaction action;
	struct messages msgs = {
		.font_path = DEFAULT_FONT_PATH,
		.font_size_pt = FONT_SIZE_PT,
		.font_size_b_pt = FONT_SIZE_B_PT,
		.msg = NULL,
		.bottom_msg = NULL,
	};
	struct dpi_info dpi_info = {
		.dpi = 0,
		.pixels_per_milli = 0,
		.logo_size_px = 0,
		.logo_size_max_mm = LOGO_SIZE_MAX_MM,
	};
	struct image_info image_info = {
		.path = NULL,
		.image = NULL,
		.width = 0,
		.height = 0,
		.x = 0,
		.y = 0,
	};
	int optflag;
	bool animation = true;

	memset(active_tty, '\0', TTY_PATH_LEN);
	strcat(active_tty, "/dev/");

	memset(&action, 0, sizeof(action));
	action.sa_handler = term;
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);

	while ((optflag = getopt(argc, argv, "hvf:s:m:b:o:p:q:d:e")) != -1) {
		char *end = NULL;
		switch (optflag) {
		case 'h':
			return usage();
		case 'v':
			debug = true;
			break;
		case 'f':
			msgs.font_path = optarg;
			break;
		case 's':
			image_info.path = optarg;
			break;
		case 'm':
			message = malloc(strlen(optarg) + 1);
			strcpy(message, optarg);
			break;
		case 'b':
			message_bottom = malloc(strlen(optarg) + 1);
			strcpy(message_bottom, optarg);
			break;
		case 'o':
			msgs.font_size_b_pt = strtof(optarg, &end);
			if (end == optarg) {
				fprintf(stderr, "Invalid font size: %s\n", optarg);
				return usage();
			}
			break;
		case 'p':
			msgs.font_size_pt = strtof(optarg, &end);
			if (end == optarg) {
				fprintf(stderr, "Invalid font size: %s\n", optarg);
				return usage();
			}
			break;
		case 'q':
			dpi_info.logo_size_max_mm = strtof(optarg, &end);
			if (end == optarg) {
				fprintf(stderr, "Invalid max logo size: %s\n", optarg);
				return usage();
			}
			break;
		case 'd':
			dpi_info.dpi = strtol(optarg, &end, 10);
			if (end == optarg || dpi_info.dpi < 0) {
				fprintf(stderr, "Invalid dpi: %s\n", optarg);
				return usage();
			}
			break;
		case 'e':
			animation = false;
			break;
		default:
			return usage();
		}
	}

	// {
	// 	FILE *fp = fopen("/sys/devices/virtual/tty/tty0/active", "r");
	// 	int len = strlen(active_tty);
	// 	char *ptr = active_tty + len;
	// 	if (fp != NULL) {
	// 		fgets(ptr, TTY_PATH_LEN - len, fp);
	// 		*(ptr + strlen(ptr) - 1) = '\0';
	// 		fclose(fp);
	// 	}
	// }

	// LOG("active tty: '%s'\n", active_tty);

	// if ((rc = tfb_acquire_fb(/*TFB_FL_NO_TTY_KD_GRAPHICS */ 0, "/dev/fb0", active_tty)) !=
	//     TFB_SUCCESS) {
	// 	fprintf(stderr, "tfb_acquire_fb() failed with error code: %d\n", rc);
	// 	rc = 1;
	// 	return rc;
	// }

	if ((rc = tfb_acquire_drm(0, "/dev/dri/card0")) != 0) {
		fprintf(stderr, "tfb_acquire_drm() failed with error code: %d\n", rc);
		rc = 1;
		return rc;
	}

	screenWidth = (int)tfb_screen_width();
	screenHeight = (int)tfb_screen_height();

	calculate_dpi_info(&dpi_info);

	rc = load_image(&dpi_info, &image_info);
	if (rc)
		goto out;

	float animation_y = image_info.y + image_info.height + MM_TO_PX(dpi_info.dpi, 5);

	tfb_clear_screen(tfb_make_color(bg.r, bg.g, bg.b));

	draw_svg(image_info.image, image_info.x, image_info.y, image_info.width, image_info.height);

	if (!message && !message_bottom)
		goto no_messages;

	struct msg_info bottom_msg, msg;

	memset(&bottom_msg, 0, sizeof(bottom_msg));
	memset(&msg, 0, sizeof(msg));

	bottom_msg.src_message = message_bottom;
	msg.src_message = message;

	if (message_bottom)
		msgs.bottom_msg = &bottom_msg;
	if (message)
		msgs.msg = &msg;

	show_messages(&msgs, &dpi_info);

no_messages:
	/* This is necessary to copy the parts we draw once (like the logo) to the front buffer */
	tfb_flush_window();
	tfb_flush_fb();

	int tick = 0;
	// int tty = open(active_tty, O_RDWR);
	// if (!tty) {
	// 	fprintf(stderr, "Failed to open tty %s (%d)\n", active_tty, errno);
	// 	goto out;
	// }

	struct timespec epoch, start, end, diff;
	int target_fps = 60;
	float tickrate = 60.0;
	clock_gettime(CLOCK_REALTIME, &epoch);
	while (!terminate) {
		if (!animation) {
			sleep(1);
			continue;
		}
		clock_gettime(CLOCK_REALTIME, &start);
		tick = timespec_to_double(timespec_sub(start, epoch)) * tickrate;
		animate_frame(tick, screenWidth, animation_y, dpi_info.dpi);
		tfb_flush_fb();
		clock_gettime(CLOCK_REALTIME, &end);
		diff = timespec_sub(end, start);
		//printf("%05d: %09ld\n", tick, diff.tv_nsec);
		if (diff.tv_nsec < 1000000000 / target_fps) {
			struct timespec sleep_time = {
				.tv_sec = 0,
				.tv_nsec = 1000000000 / target_fps - diff.tv_nsec,
			};
			nanosleep(&sleep_time, NULL);
		}
	}

out:
	// Before we exit print the logo so it will persist
	if (image_info.image) {
		//ioctl(tty, KDSETMODE, KD_TEXT);
		draw_svg(image_info.image, image_info.x, image_info.y, image_info.width,
			 image_info.height);
	}

	// Draw the messages again so they will persist
	show_messages(&msgs, &dpi_info);

	nsvgDelete(image_info.image);
	nsvgDelete(msgs.font);
	free_message(msgs.msg);
	free_message(msgs.bottom_msg);
	if (message)
		free(message);
	if (message_bottom)
		free(message_bottom);
	// The TTY might end up in a weird state if this
	// is not called!
	tfb_release_fb();
	return rc;
}
