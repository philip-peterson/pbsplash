#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
#include <fcntl.h>
#include <tfblib/tfblib.h>
#include <tfblib/tfb_colors.h>

#include <string.h>
#include <math.h>
#define NANOSVG_ALL_COLOR_KEYWORDS // Include full list of color keywords.
#include "nanosvg.h"
#include "nanosvgrast.h"

#include "pbsplash.h"

#define MSG_MAX_LEN	  4096
#define DEFAULT_FONT_PATH "/usr/share/pbsplash/OpenSans-Regular.svg"
#define LOGO_SIZE_MAX_MM  25
#define FONT_SIZE_PT	  9
#define FONT_SIZE_B_PT    5
#define PT_TO_MM	  0.38f
#define TTY_PATH_LEN	  11

#define DEBUGRENDER	  0

#define MM_TO_PX(dpi, mm) (dpi / 25.4) * (mm)

volatile sig_atomic_t terminate = 0;

bool debug = false;
struct col background_color = { .r = 0, .g = 0, .b = 0, .a = 255 };

static int screenWidth, screenHeight;

#define LOG(fmt, ...)                                                          \
	do {                                                                   \
		if (debug)                                                     \
			printf(fmt, ##__VA_ARGS__);                   \
	} while (0)

int usage()
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

void term(int signum)
{
	terminate = 1;
}

static void blit_buf(unsigned char *buf, int x, int y, int w, int h, bool vflip,
		     bool redraw)
{
	struct col prev_col = { .r = 0, .g = 0, .b = 0, .a = 0 };
	unsigned int col = tfb_make_color(
		background_color.r, background_color.g, background_color.b);

	for (size_t i = 0; i < w; i++) {
		for (size_t j = 0; j < h; j++) {
#if DEBUGRENDER == 1
			if (i == 0 || i == w - 1 || j == 0 || j == h - 1) {
				tfb_draw_pixel(x + i, y + h - j, tfb_red);
				continue;
			}
#endif
			struct col rgba =
				*(struct col *)(buf + (j * w + i) * 4);
			if (rgba.a == 0 || rgba.rgba == background_color.rgba)
				continue;

			// Alpha blending
			if (rgba.a != 255) {
				rgba.r =
					(rgba.r * rgba.a +
					 background_color.r * (255 - rgba.a)) >>
					8;
				rgba.g =
					(rgba.g * rgba.a +
					 background_color.g * (255 - rgba.a)) >>
					8;
				rgba.b =
					(rgba.b * rgba.a +
					 background_color.b * (255 - rgba.a)) >>
					8;
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

static void draw_svg(NSVGimage *image, int x, int y, int w, int h)
{
	float sz = (int)((float)w / (float)image->width * 100.f) / 100.f;
	LOG("draw_svg: %dx%d, %dx%d, %f\n", x, y, w, h, sz);
	NSVGrasterizer *rast = nsvgCreateRasterizer();
	unsigned char *img = malloc(w * h * 4);
	nsvgRasterize(rast, image, 0, 0, sz, img, w, h, w * 4);

	blit_buf(img, x, y, w, h, false, false);

	free(img);
	nsvgDeleteRasterizer(rast);
}

static void draw_text(NSVGimage *font, char *text, int x, int y, int width,
		      int height, float scale, unsigned int tfb_col)
{
	LOG("text '%s': fontsz=%f, x=%d, y=%d, dimensions: %d x %d\n", text,
	    scale, x, y, width, height);
	NSVGshape **shapes = nsvgGetTextShapes(font, text, strlen(text));
	unsigned char *img = malloc(width * height * 4);
	NSVGrasterizer *rast = nsvgCreateRasterizer();

	nsvgRasterizeText(rast, font, 0, 0, scale, img, width, height,
			  width * 4, text);

	blit_buf(img, x, y, width, height, true, false);

	free(img);
	free(shapes);
	nsvgDeleteRasterizer(rast);
}

/*
 * Get the dimensions of a string in pixels.
 * based on the font size and the font SVG file.
 */
static char* getTextDimensions(NSVGimage *font, char *text, float scale,
			      int *width, int *height)
{
	int i;
	int fontHeight = (font->fontAscent - font->fontDescent) * scale;
	int maxWidth = 0;
	char *out_text = malloc(strlen(text));

	if (text == NULL)
		return text;

	*width = 0;
	// The height is simply the height of the font * the scale factor
	*height = fontHeight;

	NSVGshape **shapes = nsvgGetTextShapes(font, text, strlen(text));
	// Iterate over every glyph in the string to get the total width
	for (i = 0; text[i] != '\0'; i++) {
		NSVGshape *shape = shapes[i];
		out_text[i] = text[i];
		if (*width > screenWidth * 0.95) {
			while (out_text[i] != ' ' && i > 0)
				i--;
			out_text[i] = '\n';
		}

		if (out_text[i] == '\n') {
			*height += fontHeight;
			maxWidth = *width > maxWidth ? *width : maxWidth;
			*width = 0;
			continue;
		}

		if (shape) {
			*width += (float)shapes[i]->horizAdvX * scale + 0.5;
		} else {
			*width += font->defaultHorizAdv * scale;
		}
	}

	*width = *width > maxWidth ? *width : maxWidth;

	free(shapes);
	return out_text;
}

int main(int argc, char **argv)
{
	int rc = 0;
	char *message = NULL;
	char *message_bottom = NULL;
	char *splash_image = NULL;
	char *font_path = DEFAULT_FONT_PATH;
	char active_tty[TTY_PATH_LEN + 1];
	NSVGimage *image = NULL;
	NSVGimage *font = NULL;
	struct sigaction action;
	float font_size = FONT_SIZE_PT;
	float font_size_b = FONT_SIZE_B_PT;
	float logo_size_max = LOGO_SIZE_MAX_MM;
	int optflag;
	long dpi = 0;
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
			font_path = optarg;
			break;
		case 's':
			splash_image = optarg;
			break;
		case 'm':
			message = optarg;
			break;
		case 'b':
			message_bottom = optarg;
			break;
		case 'o':
			font_size_b = strtof(optarg, &end);
			if (end == optarg) {
				fprintf(stderr, "Invalid font size: %s\n",
					optarg);
				return usage();
			}
			break;
		case 'p':
			font_size = strtof(optarg, &end);
			if (end == optarg) {
				fprintf(stderr, "Invalid font size: %s\n",
					optarg);
				return usage();
			}
			break;
		case 'q':
			logo_size_max = strtof(optarg, &end);
			if (end == optarg) {
				fprintf(stderr, "Invalid max logo size: %s\n",
					optarg);
				return usage();
			}
			break;
		case 'd':
			dpi = strtol(optarg, &end, 10);
			if (end == optarg) {
				fprintf(stderr, "Invalid dpi: %s\n",
					optarg);
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

	{
		FILE *fp = fopen("/sys/devices/virtual/tty/tty0/active", "r");
		int len = strlen(active_tty);
		char *ptr = active_tty + len;
		if (fp != NULL) {
			fgets(ptr, TTY_PATH_LEN - len, fp);
			*(ptr + strlen(ptr) - 1) = '\0';
			fclose(fp);
		}
	}

	LOG("active tty: '%s'\n", active_tty);

	if ((rc = tfb_acquire_fb(/*TFB_FL_NO_TTY_KD_GRAPHICS */0, "/dev/fb0",
				 active_tty)) != TFB_SUCCESS) {
		fprintf(stderr, "tfb_acquire_fb() failed with error code: %d\n",
			rc);
		rc = 1;
		return rc;
	}

	screenWidth = (int)tfb_screen_width();
	screenHeight = (int)tfb_screen_height();

	int w_mm = tfb_screen_width_mm();
	int h_mm = tfb_screen_height_mm();
	// If DPI is specified on cmdline then calculate display size from it
	// otherwise calculate the dpi based on the display size.
	if (dpi > 0) {
		w_mm = screenWidth / (float)dpi * 25.4;
		h_mm = screenHeight / (float)dpi * 25.4;
	} else {
		dpi = (float)screenWidth / (float)w_mm * 25.4;
	}
	int pixels_per_milli = (float)screenWidth / (float)w_mm;

	if (logo_size_max * pixels_per_milli > screenWidth)
		logo_size_max = (screenWidth * 0.75f) / pixels_per_milli;

	float logo_size_px = (float)(screenWidth < screenHeight ? screenWidth : screenHeight) * 0.75f;
	if (w_mm > 0 && h_mm > 0) {
		if (w_mm < h_mm) {
			if (w_mm > logo_size_max * 1.2f)
				logo_size_px = logo_size_max *
					       pixels_per_milli;
		} else {
			if (h_mm > logo_size_max * 1.2f)
				logo_size_px = logo_size_max *
					       pixels_per_milli;
		}
	}

	LOG("%dx%d @ %dx%dmm, dpi=%ld, logo_size_px=%f\n", screenWidth, screenHeight, w_mm, h_mm,
	    dpi, logo_size_px);

	image = nsvgParseFromFile(splash_image, "", logo_size_px);
	if (!image) {
		fprintf(stderr, "failed to load SVG image\n");
		rc = 1;
		goto out;
	}

	if (image->width < image->height * 1.5)
		logo_size_px = MM_TO_PX(dpi, 25);

	float sz =
		(float)logo_size_px /
		(image->width > image->height ? image->height : image->width);
	float image_w = image->width * sz + 0.5;
	float image_h = image->height * sz + 0.5;
	if (image_w > (logo_size_max * pixels_per_milli)) {
		float scalefactor = ((float)(logo_size_max * pixels_per_milli)/ image_w);
		printf("Got scale factor: %f\n", scalefactor);
		image_w = logo_size_max * pixels_per_milli;
		image_h *= scalefactor;
	}
	float x = (float)screenWidth / 2;
	float y = (float)screenHeight / 2;
	// Center the image
	x -= image_w * 0.5f;
	y -= image_h * 0.5f;
	float animation_y = y + image_h + MM_TO_PX(dpi, 5);

	tfb_clear_screen(tfb_make_color(background_color.r, background_color.g,
					background_color.b));

	draw_svg(image, x, y, image_w, image_h);

	if (message || message_bottom) {
		int textWidth, textHeight, bottomTextHeight = MM_TO_PX(dpi, 5);
		float fontsz;
		int tx, ty;

		font = nsvgParseFromFile(font_path, "px", 512);
		if (!font || !font->shapes) {
			fprintf(stderr, "failed to load SVG font\n");
			rc = 1;
			goto out;
		}

		if (message_bottom) {
			fontsz = ((float)font_size_b * PT_TO_MM) /
				(font->fontAscent - font->fontDescent) *
				pixels_per_milli;

			message_bottom = getTextDimensions(font, message_bottom, fontsz,
					&textWidth, &bottomTextHeight);
			tx = screenWidth / 2.f - textWidth / 2.f;
			ty = screenHeight - bottomTextHeight - MM_TO_PX(dpi, 2);
			draw_text(font, message_bottom, tx, ty, textWidth,
				bottomTextHeight, fontsz, tfb_gray);
		}

		if (message) {
			fontsz = ((float)font_size * PT_TO_MM) /
				(font->fontAscent - font->fontDescent) *
				pixels_per_milli;
			LOG("Fontsz: %f\n", fontsz);
			message = getTextDimensions(font, message, fontsz, &textWidth,
					&textHeight);

			tx = screenWidth / 2.f - textWidth / 2.f;
			ty = screenHeight - bottomTextHeight - MM_TO_PX(dpi, font_size_b * PT_TO_MM * 2) - textHeight;

			draw_text(font, message, tx, ty, textWidth, textHeight, fontsz,
				tfb_gray);
		}


	}

	tfb_flush_window();
	tfb_flush_fb();

	int frame = 0;
	int tty = open(active_tty, O_RDWR);
	while (!terminate) {
		if (!animation) {
			sleep(1);
			continue;
		}
		animate_frame(frame++, screenWidth, animation_y, dpi);
		tfb_flush_fb();
	}

out:
	// Before we exit print the logo so it will persist
	if (image) {
		ioctl(tty, KDSETMODE, KD_TEXT);
		draw_svg(image, x, y, image_w, image_h);
	}

	nsvgDelete(font);
	nsvgDelete(image);
	// The TTY might end up in a weird state if this
	// is not called!
	tfb_release_fb();
	return rc;
}
