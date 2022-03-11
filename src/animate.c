#include <math.h>
#include <stdio.h>
#include <tfblib/tfblib.h>
#include <tfblib/tfb_colors.h>
#include "pbsplash.h"

struct col color = {.r = 255, .g = 255, .b = 255, .a = 255};

// FIXME: calculate constants based on display size/resolution

#define n_circles 5
#define amplitude 40
#define rad 12

void animate_frame(int frame, int w, int y_off)
{
    unsigned int t_col = tfb_make_color(color.r, color.g, color.b);
    for (unsigned int i = 0; i < n_circles; i++)
    {
        int c_dist = w * 0.05;
        int x = i * c_dist + w / 2 - c_dist * n_circles / 2.f;
        double s = sin(frame / 30.0 * 3.1415 + i * 0.5);
        int y = y_off + s * amplitude;
        tfb_fill_rect(x - rad- 1, y_off - amplitude - rad, rad* 2 + 2, 400 +rad* 2, tfb_black);
        tfb_fill_circle(x, y, rad, t_col);
    }
}
