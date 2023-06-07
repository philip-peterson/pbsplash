#include "pbsplash.h"
#include <math.h>
#include <stdio.h>

#include "tfblib.h"

struct col color = { .r = 255, .g = 255, .b = 255, .a = 255 };

static void animate_logo(int frame, struct image_info *images)
{
	static int step = 0;

	tfb_draw_pixel(0, 0, tfb_black);

	if (frame > 0 && (step + 1) * 4 < frame) {
		step++;
	}

	draw_svg(images->image[step % images->num_frames], images->x, images->y, images->width,
		 images->height);
}

void animate_frame(int frame, int w, int y_off, long dpi,
		   struct image_info *images)
{
	animate_logo(frame, images);
}
