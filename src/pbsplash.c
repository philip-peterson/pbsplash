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
#define NANOSVG_IMPLEMENTATION     // Expands implementation
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

#include "pbsplash.h"

#define MSG_MAX_LEN 4096
#define DEFAULT_FONT_PATH "/usr/share/pbsplash/OpenSans-Regular.svg"
#define LOGO_SIZE_MAX_MM 90
#define FONT_SIZE_PT 9
#define PT_TO_MM 0.38f
#define TTY_PATH_LEN 11

#define DEBUGRENDER 0

volatile sig_atomic_t terminate = 0;

bool debug = true;
struct col background_color = {.r = 0, .g = 0, .b = 0, .a = 255};

#define LOG(fmt, ...)                         \
   do                                         \
   {                                          \
      if (debug)                              \
         fprintf(stdout, fmt, ##__VA_ARGS__); \
   } while (0)

int usage()
{
   fprintf(stderr, "pbsplash: postmarketOS bootsplash generator\n");
   fprintf(stderr, "-------------------------------------------\n");
   fprintf(stderr, "pbsplash [-h] [-d] [-f font] [-s splash image] [-m message]\n\n");
   fprintf(stderr, "    -v|--verbose          enable verbose logging\n");
   fprintf(stderr, "    -h|--help             show this help\n");
   fprintf(stderr, "    -f|--font             path to SVG font file (default: %s)\n", DEFAULT_FONT_PATH);
   fprintf(stderr, "    -s|--splash-image     path to splash image to display\n");
   fprintf(stderr, "    -m|--message          message to show under the splash image\n");
   fprintf(stderr, "    -p|--font-size        font size in pt (default: %d)\n", FONT_SIZE_PT);
   fprintf(stderr, "    -d|--dpi              custom DPI (for testing)\n");

   return 1;
}

void term(int signum)
{
   terminate = 1;
}

static void blit_buf(unsigned char *buf, int x, int y, int w, int h, bool vflip, bool redraw)
{
   struct col prev_col = {.r = 0, .g = 0, .b = 0, .a = 0};
   unsigned int col = tfb_make_color(background_color.r, background_color.g, background_color.b);

   for (size_t i = 0; i < w; i++)
   {
      for (size_t j = 0; j < h; j++)
      {
#if DEBUGRENDER == 1
         if (i == 0 || i == w - 1 || j == 0 || j == h - 1)
         {
            tfb_draw_pixel(x + i, y + h - j, tfb_red);
            continue;
         }
#endif
         struct col rgba = *(struct col *)(buf + (j * w + i) * 4);
         if (rgba.a == 0 || rgba.rgba == background_color.rgba)
            continue;

         // Alpha blending
         if (rgba.a != 255)
         {
            rgba.r = (rgba.r * rgba.a + background_color.r * (255 - rgba.a)) >> 8;
            rgba.g = (rgba.g * rgba.a + background_color.g * (255 - rgba.a)) >> 8;
            rgba.b = (rgba.b * rgba.a + background_color.b * (255 - rgba.a)) >> 8;
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

static void draw_text(NSVGimage *font, char *text, int x, int y, int width, int height, float scale, unsigned int tfb_col)
{
   LOG("text '%s': fontsz=%f, x=%d, y=%d, dimensions: %d x %d\n", text,
       scale, x, y, width, height);
   NSVGshape **shapes = nsvgGetTextShapes(font, text, strlen(text));
   unsigned char *img = malloc(width * height * 4);
   NSVGrasterizer *rast = nsvgCreateRasterizer();

   nsvgRasterizeText(rast, font, 0, 0, scale, img, width, height, width * 4, text);

   blit_buf(img, x, y, width, height, true, false);

   free(img);
   free(shapes);
   nsvgDeleteRasterizer(rast);
}

/*
 * Get the dimensions of a string in pixels.
 * based on the font size and the font SVG file.
 */
static void getTextDimensions(NSVGimage *font, char *text, float scale, int *width, int *height)
{
   int i = 0;

   *width = 0;
   // The height is simply the height of the font * the scale factor
   *height = (font->fontAscent - font->fontDescent) * scale;
   if (text == NULL)
      return;

   NSVGshape **shapes = nsvgGetTextShapes(font, text, strlen(text));
   // Iterate over every glyph in the string to get the total width
   for (i = 0; i < strlen(text); i++)
   {
      NSVGshape *shape = shapes[i];
      if (shape)
      {
         *width += (float)shapes[i]->horizAdvX * scale + 0.5;
      }
      else
      {
         *width += font->defaultHorizAdv * scale;
      }
   }

   free(shapes);
}

int main(int argc, char **argv)
{
   int rc = 0;
   char *message = NULL;
   char *splash_image = NULL;
   char *font_path = DEFAULT_FONT_PATH;
   char active_tty[TTY_PATH_LEN + 1];
   NSVGimage *image = NULL;
   NSVGimage *font = NULL;
   struct sigaction action;
   float font_size = FONT_SIZE_PT;
   int optflag;
   long dpi = 0;

   memset(active_tty, '\0', TTY_PATH_LEN);
   strcat(active_tty, "/dev/");

   memset(&action, 0, sizeof(action));
   action.sa_handler = term;
   sigaction(SIGTERM, &action, NULL);
   sigaction(SIGINT, &action, NULL);

   while ((optflag = getopt(argc, argv, "hvf:s:m:p:d:")) != -1)
   {
      char *end = NULL;
      switch (optflag)
      {
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
      case 'p':
         font_size = strtof(optarg, &end);
         if (end == optarg)
         {
            fprintf(stderr, "Invalid font size: %s\n", optarg);
            return usage();
         }
         break;
      case 'd':
         if (!optarg)
         {
            fprintf(stderr, "--dpi requires an argument\n");
            return usage();
         }
         dpi = strtol(optarg, &end, 10);
         if (end == optarg)
         {
            fprintf(stderr, "Invalid font size: %s\n", optarg);
            return usage();
         }
         break;
      default:
         return usage();
      }
   }

   {
      FILE *fp = fopen("/sys/devices/virtual/tty/tty0/active", "r");
      int len = strlen(active_tty);
      char *ptr = active_tty + len;
      if (fp != NULL)
      {
         fgets(ptr, TTY_PATH_LEN - len, fp);
         *(ptr + strlen(ptr) - 1) = '\0';
         fclose(fp);
      }
   }

   LOG("active tty: '%s'\n", active_tty);

   if ((rc = tfb_acquire_fb(/*TFB_FL_USE_DOUBLE_BUFFER*/ 0, "/dev/fb0", active_tty)) != TFB_SUCCESS)
   {
      fprintf(stderr, "tfb_acquire_fb() failed with error code: %d\n", rc);
      rc = 1;
      return rc;
   }

   int w = (int)tfb_screen_width();
   int h = (int)tfb_screen_height();

   int w_mm = tfb_screen_width_mm();
   int h_mm = tfb_screen_height_mm();
   // If DPI is specified on cmdline then calculate display size from it
   // otherwise calculate the dpi based on the display size.
   if (dpi > 0)
   {
      w_mm = w / (float)dpi * 25.4;
      h_mm = h / (float)dpi * 25.4;
   }
   else
   {
      dpi = (float)w / (float)w_mm * 25.4;
   }
   int pixels_per_milli = (float)w / (float)w_mm;

   float logo_size_px = (float)(w < h ? w : h) * 0.75f;
   if (w_mm > 0 && h_mm > 0)
   {
      if (w_mm < h_mm)
      {
         if (w_mm > (float)LOGO_SIZE_MAX_MM * 1.2f)
            logo_size_px = (float)LOGO_SIZE_MAX_MM * pixels_per_milli;
      }
      else
      {
         if (h_mm > (float)LOGO_SIZE_MAX_MM * 1.2f)
            logo_size_px = (float)LOGO_SIZE_MAX_MM * pixels_per_milli;
      }
   }

   LOG("%dx%d @ %dx%dmm, dpi=%ld, logo_size_px=%f\n", w, h, w_mm, h_mm, dpi, logo_size_px);

   image = nsvgParseFromFile(splash_image, "", logo_size_px);
   if (!image)
   {
      fprintf(stderr, "failed to load SVG image\n");
      rc = 1;
      goto out;
   }

   float sz = (float)logo_size_px / (image->width > image->height ? image->width : image->height);
   int image_w = image->width * sz + 0.5;
   int image_h = image->height * sz + 0.5;
   float x = (float)w / 2;
   float y = (float)h / 2;
   // Center the image
   x -= image_w * 0.5f;
   y -= image_h * 0.5f;

   tfb_clear_screen(tfb_make_color(background_color.r, background_color.g, background_color.b));

   draw_svg(image, x, y, image_w, image_h);

   if (message)
   {
      int textWidth, textHeight;

      font = nsvgParseFromFile(font_path, "px", 512);
      if (!font || !font->shapes)
      {
         fprintf(stderr, "failed to load SVG font\n");
         rc = 1;
         goto out;
      }

      float fontsz = ((float)font_size * PT_TO_MM) / (font->fontAscent - font->fontDescent) * pixels_per_milli;

      getTextDimensions(font, message, fontsz, &textWidth, &textHeight);

      int tx = w / 2.f - textWidth / 2.f;
      int ty = y + image_h + textHeight * 0.5f;

      draw_text(font, message, tx, ty, textWidth, textHeight, fontsz, tfb_gray);
   }

   tfb_flush_window();
   tfb_flush_fb();
#define ANIM_HEIGHT 600
   int frame = 0;
   int tty = open(active_tty, O_RDWR);
   int tty_mode = 0;
   while (!terminate)
   {
      animate_frame(frame++, w, h * 0.8);
      tfb_flush_fb();
      ioctl(tty, KDGETMODE, &tty_mode);
      // Login started and has reset the TTY back to text mode
      if (tty_mode == KD_TEXT)
      {
         // tfb_flush_window();
         draw_svg(image, x, y, image_w, image_h);
         goto out;
      }
      // usleep(1666);
   }

   // free(animation_buf);

out:
   nsvgDelete(font);
   nsvgDelete(image);
   // The TTY might end up in a weird state if this
   // is not called!
   tfb_release_fb();
   return rc;
}
