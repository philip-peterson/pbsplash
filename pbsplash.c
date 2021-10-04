#include <stdio.h>
#include <tfblib/tfblib.h>
#include <tfblib/tfb_colors.h>
#include <unistd.h>

#include "logo.h"

int main(int argc, char **argv)
{
   int rc;
   if ((rc = tfb_acquire_fb(TFB_FL_USE_DOUBLE_BUFFER, "/dev/fb0", "/dev/tty1")) != TFB_SUCCESS) {
      fprintf(stderr, "tfb_acquire_fb() failed with error code: %d\n", rc);
      return 1;
   }

   int w = (int) tfb_screen_width();
   int h = (int) tfb_screen_height();
   int sz = (float)(w < h ? w : h) * 0.5;
   fprintf(stdout, "w=%du, h=%du\n", w, h);
   float x = (float)w / 2 - sz / 2;
   float y = (float)h / 2 - sz / 2 - 300;
   float textX = x;
   float textY = y;

   while(true) {
      /* Paint the whole screen in black */
      tfb_clear_screen(tfb_black);
      /* Draw a red rectangle at the center of the screen */
      /* Draw some text on-screen */
      tfb_draw_string_scaled(x + 75, y + sz + 40, tfb_white, tfb_black, 4, 4, "postmarketOS");

      for (size_t i = 0; i < sizeof(logo) / sizeof(logo[0]); i+=4)
      {
         int x1 = (int) (logo[i] / 100 * sz) + (int)x;
         int y1 = (int) (logo[i+1] / 100 * sz) + (int)y;
         int x2 = (int) (logo[i+2] / 100 * sz) + (int)x;
         int y2 = (int) (logo[i+3] / 100 * sz) + (int)y;
         //printf("%f == ", logo[i] / 100 * w);
         printf("x1: %d, y1: %d, x2: %d, y2: %d\n", x1, y1, x1, y2);
         tfb_draw_line(x1, y1, x2, y2, tfb_green);
      }

      tfb_flush_window();
      tfb_flush_fb();
      usleep(50000);
   }

   getchar();
   tfb_release_fb();
   return 0;
}
