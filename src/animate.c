#include <math.h>
#include <stdio.h>
#include <tfblib/tfblib.h>
#include <tfblib/tfb_colors.h>
#include "pbsplash.h"

struct col color = { .r = 255, .g = 255, .b = 255, .a = 255 };

#define PI	  3.1415926535897932384626433832795

#define n_circles 3

#define speed	  5

void circles_wave(int frame, int w, int y_off, long dpi)
{
	unsigned int t_col = tfb_make_color(color.r, color.g, color.b);
	int f = frame * speed;

	int rad = (int)(dpi * 3 / 96.0);
	int dist = rad * 3;
	int amplitude = rad * 1;

	int left = ((float)w / 2) - (dist * (n_circles - 1) / 2.0);
	for (unsigned int i = 0; i < n_circles; i++) {
		int x = left + (i * dist);
		double offset = sin(f / 60.0 * PI + i);
		int y = y_off + offset * amplitude;
		tfb_fill_rect(x - rad - 3, y_off - amplitude - rad - 3,
			      rad * 2 + 6, amplitude * 2 + rad * 2 + 6,
			      tfb_black);
		tfb_fill_circle(x, y, rad, t_col);
		// tfb_draw_circle(x, y, rad, t_col);
		// tfb_draw_circle(x, y, rad+1, t_col);
		// tfb_draw_circle(x, y, rad+2, t_col);
	}
}

void animate_frame(int frame, int w, int y_off, long dpi)
{
	circles_wave(frame, w, y_off, dpi);
}
