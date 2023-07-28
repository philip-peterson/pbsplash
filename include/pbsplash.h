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

void animate_frame(int frame, int w, int y_off, long dpi);

#endif
