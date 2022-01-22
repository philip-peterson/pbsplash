#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <tfblib/tfblib.h>
#include <tfblib/tfb_colors.h>

#include <string.h>
#include <math.h>
#define NANOSVG_ALL_COLOR_KEYWORDS // Include full list of color keywords.
#define NANOSVG_IMPLEMENTATION     // Expands implementation
#include "nanosvg.h"

#include "logo.h"

#define MSG_MAX_LEN 4096

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
      tfb_draw_line(x, y, x1, y1, tfb_green);
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
   vertex2f(xoff + pts[0] * scale_x, yoff + pts[1] * scale_y, true);
   for (i = 0; i < npts-1; i += 3) {
      float* p = &pts[i*2];
      // fprintf(stdout, "ptx0: %f, pty0: %f, ptx1: %f, pty1: %f, ptx2: %f, pty2: %f, ptx3: %f, pty3: %f\n", p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
      cubicBez(xoff + p[0] * scale_x, yoff + p[1] * scale_y, xoff + p[2] * scale_x, yoff + p[3] * scale_y, xoff + p[4] * scale_x, yoff + p[5] * scale_y, xoff + p[6] * scale_x, yoff + p[7] * scale_y, tol, 0);
   }

   if (closed) {
      vertex2f(xoff + pts[0] * scale_x, yoff + pts[1] * scale_y, false);
   }
}

// Blit an SVG to the framebuffer using tfblib. The image is
// scaled based on the target width and height.
static void blit_svg(struct NSVGimage *image, int x, int y, int width, int height)
{
   struct NSVGshape *shape;
   struct NSVGpath *path;
   fprintf(stdout, "size: %f x %f\n", image->width, image->height);
   float sz_x = (float)width / image->width;
   float sz_y = (float)height / image->height;
   fprintf(stdout, "scale: %f x %f\n", sz_x, sz_y);

   for (shape = image->shapes; shape != NULL; shape = shape->next)
   {
      for (path = shape->paths; path != NULL; path = path->next)
      {
         drawPath(path->pts, path->npts, x, y, sz_x, sz_y, path->closed, 0.1f);
      }
   }
}

int main(int argc, char **argv)
{
   int rc = 0;
   int opt;
   char *message = calloc(MSG_MAX_LEN, 1);
   char *splash_image;
   struct NSVGimage *image;

   for (int i = 1; i < argc; i++)
   {
      printf("%d: %s\n", i, argv[i]);
      if (strcmp(argv[i], "-h") == 0)
      {
         rc = usage();
         goto out;
      }

      if (strcmp(argv[i], "-s") == 0)
      {
         splash_image = argv[i + 1];
         printf("splash_image path: %s\n", splash_image);
         continue;
      }

      if (strcmp(argv[i], "-m") == 0)
      {
         if (*argv[i + 1] == '-')
         {
            rc = read(STDIN_FILENO, message, MSG_MAX_LEN);
            if (rc < 0)
               goto out;
            printf("%s\n", message);
         }
         else
         {
            free(message);
            message = argv[i + 1];
         }
         printf("message: %s\n", message);
         continue;
      }
   }

   if ((rc = tfb_acquire_fb(0, "/dev/fb0", "/dev/tty1")) != TFB_SUCCESS)
   {
      fprintf(stderr, "tfb_acquire_fb() failed with error code: %d\n", rc);
      rc = 1;
      goto out;
   }

   int w = (int)tfb_screen_width();
   int h = (int)tfb_screen_height();
   float sz = (float)(w < h ? w : h) * 0.6f;
   fprintf(stdout, "w=%du, h=%du\n", w, h);
   float x = (float)w / 2 - sz / 2;
   float y = (float)h / 2 - sz / 2 - 300;
   fprintf(stdout, "x=%f, y=%f, sz=%f\n", x, y, sz);

   image = nsvgParseFromFile(splash_image, "px", 96);

   /* Paint the whole screen in black */
   tfb_clear_screen(tfb_black);
   /* Draw a red rectangle at the center of the screen */
   /* Draw some text on-screen */
   tfb_draw_string_scaled(x + 75, y + sz + 40, tfb_white, tfb_black, 4, 4, "postmarketOS");

   if (message)
   {
      tfb_draw_string_scaled_wrapped(50, (int)((float)h * 0.6), tfb_white, tfb_black, 2, 2, 80, message);
   }

   blit_svg(image, x, y, sz, sz);

   tfb_flush_window();
   tfb_flush_fb();

   sleep(20);
out:
   nsvgDelete(image);
   tfb_release_fb();
   free(message);
   return rc;
}
