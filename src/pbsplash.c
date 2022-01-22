#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <tfblib/tfblib.h>
#include <tfblib/tfb_colors.h>

#include "logo.h"

#define MSG_MAX_LEN 4096

int usage() {
   fprintf(stderr, "pbsplash: a simple fbsplash tool\n");
   fprintf(stderr, "--------------------------------\n");
   fprintf(stderr, "pbsplash [-h] [-s splash image] [-m message]\n\n");
   fprintf(stderr, "    -h          show this help\n");
   fprintf(stderr, "    -s          path to splash image to display\n");
   fprintf(stderr, "    -m          message to show under the splash image\n");

   return 1;
}

int main(int argc, char **argv)
{
   int rc = 0;
   int opt;
   char *message = calloc(MSG_MAX_LEN, 1);
   char *splash_image;

   for (int i = 1; i < argc; i++)
   {
      printf("%d: %s\n", i, argv[i]);
      if (strcmp(argv[i], "-h") == 0) {
         rc = usage();
         goto out;
      }

      if (strcmp(argv[i], "-s") == 0) {
         splash_image = argv[1];
         printf("splash_image path: %s\n", splash_image);
         continue;
      }

      if (strcmp(argv[i], "-m") == 0) {
         if (*argv[i+1] == '-') {
            rc = read(STDIN_FILENO, message, MSG_MAX_LEN);
            if (rc < 0)
               goto out;
            printf("%s\n", message);
         } else {
            free(message);
            message = argv[i+1];
         }
         printf("message: %s\n", message);
         continue;
      }
   }

   if ((rc = tfb_acquire_fb(0, "/dev/fb0", "/dev/tty1")) != TFB_SUCCESS) {
      fprintf(stderr, "tfb_acquire_fb() failed with error code: %d\n", rc);
      rc = 1;
      goto out;
   }

   int w = (int) tfb_screen_width();
   int h = (int) tfb_screen_height();
   int sz = (float)(w < h ? w : h) * 0.5;
   fprintf(stdout, "w=%du, h=%du\n", w, h);
   float x = (float)w / 2 - sz / 2;
   float y = (float)h / 2 - sz / 2 - 300;
   float textX = x;
   float textY = y;

   /* Paint the whole screen in black */
   tfb_clear_screen(tfb_black);
   /* Draw a red rectangle at the center of the screen */
   /* Draw some text on-screen */
   tfb_draw_string_scaled(x + 75, y + sz + 40, tfb_white, tfb_black, 4, 4, "postmarketOS");

   if (message) {
      tfb_draw_string_scaled_wrapped(50, (int) ((float)h * 0.6), tfb_white, tfb_black, 2, 2, 80, message);
   }

   for (size_t i = 0; i < sizeof(logo) / sizeof(logo[0]); i+=4)
   {
      int x1 = (int) (logo[i] / 100 * sz) + (int)x;
      int y1 = (int) (logo[i+1] / 100 * sz) + (int)y;
      int x2 = (int) (logo[i+2] / 100 * sz) + (int)x;
      int y2 = (int) (logo[i+3] / 100 * sz) + (int)y;
      //printf("x1: %d, y1: %d, x2: %d, y2: %d\n", x1, y1, x1, y2);
      tfb_draw_line(x1, y1, x2, y2, tfb_green);
   }

   tfb_flush_window();
   tfb_flush_fb();

   sleep(20);
out:
   tfb_release_fb();
   free(message);
   return rc;
}
