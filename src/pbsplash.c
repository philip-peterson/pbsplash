#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
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

#define MSG_MAX_LEN 4096
#define DEFAULT_FONT_PATH "/usr/share/pbsplash/OpenSans-Regular.svg"
#define LOGO_SIZE_MAX_MM 90
#define FONT_SIZE_PT 13
#define PT_TO_MM 0.38f

volatile sig_atomic_t terminate = 0;

bool debug = false;

#define LOG(fmt, ...) do { if (debug) fprintf(stdout, fmt, ##__VA_ARGS__); } while (0)

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
   printf("Caught\n");
   terminate = 1;
}


// Blit an SVG to the framebuffer using tfblib. The image is
// scaled based on the target width and height.
static void draw_svg(NSVGimage *image, int x, int y, int largest_bound)
{
   //fprintf(stdout, "size: %f x %f\n", image->width, image->height);
   float sz = (float)largest_bound / (image->width > image->height ? image->width : image->height);
   int w = image->width * sz + 0.5;
   int h = image->height * sz + 0.5;
   //fprintf(stdout, "scale: %f\n", sz);

   NSVGrasterizer *rast = nsvgCreateRasterizer();
   unsigned char *img = malloc(w * h * 4);
   nsvgRasterize(rast, image, 0, 0, sz, img, w, h, w * 4);

   for (size_t i = 0; i < w; i++)
   {
      for (size_t j = 0; j < h; j++)
      {
         unsigned int col = tfb_make_color(img[(j * w + i) * 4 + 0], img[(j * w + i) * 4 + 1], img[(j * w + i) * 4 + 2]);
         tfb_draw_pixel(x + i, y + j, col);
      }
   }

   free(img);
}

/**
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
      if (shape) {
         *width += shapes[i]->horizAdvX * scale + 1;
      } else {
         *width += font->defaultHorizAdv * scale;
      }
      
   }
}

static void draw_text(NSVGimage *font, char *text, int x, int y, int width, int height, float scale, unsigned int tfb_col)
{
   LOG("text '%s'\nfontsz=%f, x=%d, y=%d, dimensions: %d x %d\n", text,
         scale, x, y, width, height);
   NSVGshape **shapes = nsvgGetTextShapes(font, text, strlen(text));
   unsigned char *img = malloc(width * height * 4);
   NSVGrasterizer *rast = nsvgCreateRasterizer();

   nsvgRasterizeText(rast, font, 0, 0, scale, img, width, height, width * 4, text);

   for (size_t i = 0; i < width; i++)
   {
      for (size_t j = 0; j < height; j++)
      {
         unsigned int col = tfb_make_color(img[(j * width + i) * 4 + 0], img[(j * width + i) * 4 + 1], img[(j * width + i) * 4 + 2]);
         if (col != tfb_black)
            tfb_draw_pixel(x + i, y + height - j, tfb_col);
      }
   }

   free(img);
   free(shapes);
}

int main(int argc, char **argv)
{
   int rc = 0;
   char *message = NULL;
   char *splash_image = NULL;
   char *font_path = DEFAULT_FONT_PATH;
   NSVGimage *image;
   NSVGimage *font;
   struct sigaction action;
   float font_size = FONT_SIZE_PT;
   int c;
   long dpi = 0;

   memset(&action, 0, sizeof(action));
   action.sa_handler = term;
   sigaction(SIGTERM, &action, NULL);
   sigaction(SIGINT, &action, NULL);

   while ((c = getopt(argc, argv, "hvdf:s:m:p:d:")) != -1)
   {
      char *end = NULL;
      switch (c)
      {
      case 'h':
         return usage();
      case 'v':
      case 'd':
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
         dpi = strtol(optarg, &end);
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

   if ((rc = tfb_acquire_fb(0, "/dev/fb0", "/dev/tty1")) != TFB_SUCCESS)
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
   } else {
      dpi = (float)w / (float)w_mm * 25.4;
   }
   int pixels_per_milli = (float)w / (float)w_mm;

   float logo_size_px = (float)(w < h ? w : h) * 0.75f;
   if (w_mm > 0 && h_mm > 0) {
      if (w_mm < h_mm) {
         if (w_mm > (float)LOGO_SIZE_MAX_MM * 1.2f)
            logo_size_px = (float)LOGO_SIZE_MAX_MM * pixels_per_milli;
      } else {
         if (h_mm > (float)LOGO_SIZE_MAX_MM * 1.2f)
            logo_size_px = (float)LOGO_SIZE_MAX_MM * pixels_per_milli;
      }
   }

   LOG("%dx%d @ %dx%dmm, spi=%ld, logo_size_px=%f\n", w, h, w_mm, h_mm, dpi, logo_size_px);
   float x = (float)w / 2 - logo_size_px / 2;
   float y = (float)h / 2 - logo_size_px / 2;

   image = nsvgParseFromFile(splash_image, "px", 500);
   if (!image)
   {
      fprintf(stderr, "failed to load SVG image\n");
      rc = 1;
      goto release_fb;
   }
   font = nsvgParseFromFile(font_path, "px", 500);
   if (!font || !font->shapes)
   {
      fprintf(stderr, "failed to load SVG font\n");
      rc = 1;
      goto free_image;
   }

   tfb_clear_screen(tfb_black);

   draw_svg(image, x, y, logo_size_px);

   if (message) {
      int textWidth, textHeight;
      float fontsz = pixels_per_milli / (float)(font->fontAscent - font->fontDescent)
         * (font_size * PT_TO_MM);

      getTextDimensions(font, message, fontsz, &textWidth, &textHeight);

      int tx = w / 2.f - textWidth / 2.f;
      int ty = y + logo_size_px * 0.5f + textHeight * 0.5f;

      draw_text(font, message, tx, ty, textWidth, textHeight, fontsz, tfb_gray);
   }

   tfb_flush_window();
   tfb_flush_fb();
 
   while (!terminate)
   {
      sleep(1);
   }

   nsvgDelete(font);
free_image:
   nsvgDelete(image);
release_fb:
   // The TTY might end up in a weird state if this
   // is not called!
   tfb_release_fb();
   return rc;
}
