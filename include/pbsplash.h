#ifndef __pbsplash_h__
#define __pbsplash_h__

#define MM_TO_PX(dpi, mm) (dpi / 25.4) * (mm)

#define ARRAY_SIZE(a) ((int)(sizeof(a) / sizeof(a[0])))
#define INT_ABS(x) ((x) > 0 ? (x) : (-(x)))

#define MIN(x, y)                       \
	({                              \
		__typeof__(x) _x = (x); \
		__typeof__(y) _y = (y); \
		_x <= _y ? _x : _y;     \
	})

#define MAX(x, y)                       \
	({                              \
		__typeof__(x) _x = (x); \
		__typeof__(y) _y = (y); \
		_x > _y ? _x : _y;      \
	})

struct col {
	union {
		unsigned int rgba;
		struct {
			unsigned char r, g, b, a;
		};
	};
};

struct dpi_info {
	long dpi;
	int pixels_per_milli;
	float logo_size_px;
	int logo_size_max_mm;
};

typedef struct NSVGimage NSVGimage;

#define MAX_FRAMES 16

struct image_info {
	char *path[MAX_FRAMES];
	NSVGimage *image[MAX_FRAMES];
	int num_frames;
	float width;
	float height;
	float x;
	float y;
};

void draw_svg(NSVGimage *image, int x, int y, int w, int h);

void animate_frame(int frame, int w, int y_off, long dpi, struct image_info *images);

#endif
