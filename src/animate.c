#include "pbsplash.h"
#include <math.h>
#include <stdio.h>

#include "tfblib.h"

struct col color = { .r = 255, .g = 255, .b = 255, .a = 255 };

#define PI 3.1415926535897932384626433832795
#define n_circles 3
#define speed 2.5

static void circles_wave(int frame, int w, int y_off, long dpi)
{
	unsigned int t_col = tfb_make_color(color.r, color.g, color.b);
	int f = round(frame * speed);

	int rad = MM_TO_PX(dpi, 1);
	int dist = rad * 3.5;
	int amplitude = rad * 1;
	int left = ((float)w / 2) - (dist * (n_circles - 1) / 2.0);

	for (unsigned int i = 0; i < n_circles; i++) {
		int x = left + (i * dist);
		double offset = sin(f / 60.0 * PI + i);
		int y = y_off + offset * amplitude;
		tfb_fill_rect(x - rad - 3, y_off - amplitude - rad - 3, rad * 2 + 6,
			      amplitude * 2 + rad * 2 + 6, tfb_black);
		tfb_fill_circle(x, y, rad, t_col);
	}

	// tfb_draw_line(0, 0, 100, 500, tfb_red);
	// tfb_draw_line(100, 0, 100, 500, tfb_green);
	// tfb_draw_line(200, 0, 100, 500, tfb_blue);

	// tfb_fill_rect(400, 300, 200, 500, tfb_red);
	// tfb_fill_rect(600, 300, 200, 500, tfb_green);
	// tfb_fill_rect(800, 300, 200, 500, tfb_blue);
}

void animate_frame(int frame, int w, int y_off, long dpi)
{
	circles_wave(frame, w, y_off, dpi);
}
