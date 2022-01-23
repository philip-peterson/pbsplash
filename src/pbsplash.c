#include <stdio.h>
#include <stdlib.h>
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
#define FONT_PATH "/usr/share/pbsplash/OpenSans-Regular.svg"

int usage()
{
   fprintf(stderr, "pbsplash: a simple fbsplash tool\n");
   fprintf(stderr, "--------------------------------\n");
   fprintf(stderr, "pbsplash [-h] [-s splash image] [-m message]\n\n");
   fprintf(stderr, "    -h          show this help\n");
   fprintf(stderr, "    -s          path to splash image to display\n");
   fprintf(stderr, "    -m          message to show under the splash image\n");

   return 1;
}

static float distPtSeg(float x, float y, float px, float py, float qx, float qy)
{
	float pqx, pqy, dx, dy, d, t;
	pqx = qx-px;
	pqy = qy-py;
	dx = x-px;
	dy = y-py;
	d = pqx*pqx + pqy*pqy;
	t = pqx*dx + pqy*dy;
	if (d > 0) t /= d;
	if (t < 0) t = 0;
	else if (t > 1) t = 1;
	dx = px + t*pqx - x;
	dy = py + t*pqy - y;
	return dx*dx + dy*dy;
}

static void vertex2f(float x, float y, bool reset)
{
   static float x1 = 0.0f;
   static float y1 = 0.0f;
   static bool first = true;

   if (reset) {
      x1 = 0.0f;
      x1 = 0.0f;
      first = true;
   }

   if (!first)
      tfb_draw_line(x1, y1, x, y, tfb_green);
   else
      first = false;

   x1 = x;
   y1 = y;
}

static void cubicBez(float x1, float y1, float x2, float y2,
                     float x3, float y3, float x4, float y4,
                     float tol, int level)
{
   float x12, y12, x23, y23, x34, y34, x123, y123, x234, y234, x1234, y1234;
   float d;

   if (level > 25) {
      fprintf(stdout, "error: max subdivision level reached\n");
      return;
   }

   x12 = (x1 + x2) * 0.5f;
   y12 = (y1 + y2) * 0.5f;
   x23 = (x2 + x3) * 0.5f;
   y23 = (y2 + y3) * 0.5f;
   x34 = (x3 + x4) * 0.5f;
   y34 = (y3 + y4) * 0.5f;
   x123 = (x12 + x23) * 0.5f;
   y123 = (y12 + y23) * 0.5f;
   x234 = (x23 + x34) * 0.5f;
   y234 = (y23 + y34) * 0.5f;
   x1234 = (x123 + x234) * 0.5f;
   y1234 = (y123 + y234) * 0.5f;

   d = distPtSeg(x1234, y1234, x1, y1, x4, y4);
   if (d > tol * tol)
   {
      cubicBez(x1, y1, x12, y12, x123, y123, x1234, y1234, tol, level + 1);
      cubicBez(x1234, y1234, x234, y234, x34, y34, x4, y4, tol, level + 1);
   }
   else
   {
      vertex2f(x4, y4, false);
   }
}

static void drawPath(float *pts, int npts, int xoff, int yoff, float scale_x, float scale_y, char closed, float tol)
{
   int i;
   vertex2f(xoff + pts[0] * scale_x, yoff - pts[1] * scale_y, true);
   for (i = 0; i < npts-1; i += 3) {
      float* p = &pts[i*2];
      // fprintf(stdout, "ptx0: %f, pty0: %f, ptx1: %f, pty1: %f, ptx2: %f, pty2: %f, ptx3: %f, pty3: %f\n", p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
      cubicBez(xoff + p[0] * scale_x, yoff - p[1] * scale_y, xoff + p[2] * scale_x, yoff - p[3] * scale_y, xoff + p[4] * scale_x, yoff - p[5] * scale_y, xoff + p[6] * scale_x, yoff - p[7] * scale_y, tol, 0);
   }

   if (closed) {
      vertex2f(xoff + pts[0] * scale_x, yoff - pts[1] * scale_y, false);
   }
}

// Blit an SVG to the framebuffer using tfblib. The image is
// scaled based on the target width and height.
static void draw_svg(NSVGimage *image, int x, int y, int width, int height)
{
   //fprintf(stdout, "size: %f x %f\n", image->width, image->height);
   float sz = (float)width / image->width;
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

static void getTextDimensions(NSVGimage *font, char *text, float scale, int *width, int *height)
{
   int i = 0;

   *width = 0;
   *height = (font->fontAscent - font->fontDescent) * scale;
   NSVGshape **shapes = nsvgGetTextShapes(font, text, strlen(text));
   for (size_t i = 0; i < strlen(text); i++)
   {
      NSVGshape *shape = shapes[i];
      if (shape) {
         *width += shapes[i]->horizAdvX * scale + 1;
      } else {
         *width += font->defaultHorizAdv * scale;
      }
      
   }
}

static void draw_text(NSVGimage *font, char *text, int x, int y, int width, int height, float scale)
{
   //printf("text: %s, scale: %f, x: %d, y: %d\n", text, scale, x, y);
   NSVGshape **shapes = nsvgGetTextShapes(font, text, strlen(text));
   unsigned char *img = malloc(width * height * 4);
   NSVGrasterizer *rast = nsvgCreateRasterizer();

   //printf("text dimensions: %d x %d\n", width, height); 

   nsvgRasterizeText(rast, font, 0, 0, scale, img, width, height, width * 4, text);

   for (size_t i = 0; i < width; i++)
   {
      for (size_t j = 0; j < height; j++)
      {
         unsigned int col = tfb_make_color(img[(j * width + i) * 4 + 0], img[(j * width + i) * 4 + 1], img[(j * width + i) * 4 + 2]);
         tfb_draw_pixel(x + i, y + height - j, col);
      }
   }

   free(img);
   free(shapes);
}

int main(int argc, char **argv)
{
   int rc = 0;
   int opt;
   char *message;
   char *logoText = "postmarketOS";
   char *splash_image;
   NSVGimage *image;
   NSVGimage *font;
   int textWidth, textHeight;

   for (int i = 1; i < argc; i++)
   {
      if (strcmp(argv[i], "-h") == 0)
      {
         rc = usage();
         return rc;
      }

      if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)
      {
         splash_image = argv[i + 1];
         //printf("splash_image path: %s\n", splash_image);
         continue;
      }

      if (strcmp(argv[i], "-m") == 0)
      {
         message = argv[i + 1];
         printf("message: %s\n", message);
         continue;
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
   float sz = (float)(w < h ? w : h) * 0.5f;
   //fprintf(stdout, "w=%du, h=%du\n", w, h);
   float x = (float)w / 2 - sz / 2;
   float y = (float)h / 2 - sz / 2 - 300;
   //fprintf(stdout, "x=%f, y=%f, sz=%f\n", x, y, sz);

   image = nsvgParseFromFile(splash_image, "px", 96);
   if (!image)
   {
      fprintf(stderr, "failed to load SVG image\n");
      rc = 1;
      return rc;
   }
   font = nsvgParseFromFile(FONT_PATH, "px", 500);
   if (!font || !font->shapes)
   {
      fprintf(stderr, "failed to load SVG font\n");
      rc = 1;
      goto free_image;
   }

   /* Paint the whole screen in black */
   tfb_clear_screen(tfb_black);

   draw_svg(image, x, y, sz, sz);

   float fontsz = (sz * 0.25) / (font->fontAscent - font->fontDescent);
   //fprintf(stdout, "font size: %f\n", fontsz);

   getTextDimensions(font, logoText, fontsz, &textWidth, &textHeight);
   
   draw_text(font, logoText, w / 2.f - textWidth / 2.f, y + sz + sz*0.2, textWidth, textHeight, fontsz);

   fontsz = (sz * 0.1) / (font->fontAscent - font->fontDescent);

   getTextDimensions(font, message, fontsz, &textWidth, &textHeight);

   draw_text(font, message, w / 2.f - textWidth / 2.f, y + sz * 2, textWidth, textHeight, fontsz);
   printf("Rendered text: %s\n", message);

   tfb_flush_window();
   tfb_flush_fb();

   //sleep(20);
out:
   nsvgDelete(font);
free_image:
   nsvgDelete(image);
   return rc;
}
