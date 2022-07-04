#ifndef __pbsplash_h__
#define __pbsplash_h__

struct col {
   union {
      unsigned int rgba;
      struct {
         unsigned char r, g, b, a;
      };
   };
};

void animate_frame(int frame, int w, int h, long dpi);

#endif
