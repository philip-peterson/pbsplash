#pragma once

#include <stdint.h>

#include "config.h"

#ifdef CONFIG_DRM_SUPPORT

#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>


struct modeset_buf {
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t size;
	uint32_t handle;
	uint8_t *map;
	uint32_t fb;
};

struct drm_framebuffer {
	unsigned int front_buf;
	struct modeset_buf bufs[2];

	drmModeModeInfo mode;
	uint32_t conn;
    uint32_t mm_width;
    uint32_t mm_height;
	uint32_t crtc;
	drmModeCrtc *saved_crtc;
};

extern struct drm_framebuffer *drm;

#endif

int drm_framebuffer_init(int *handle, const char *card);
void drm_framebuffer_close(int handle);

