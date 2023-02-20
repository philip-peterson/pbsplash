#ifndef __pbsplash_h__
#define __pbsplash_h__

#define MM_TO_PX(dpi, mm) (dpi / 25.4) * (mm)

struct col {
   union {
      unsigned int rgba;
      struct {
         unsigned char r, g, b, a;
      };
   };
};

void animate_frame(int frame, int w, int y_off, long dpi);

#endif
