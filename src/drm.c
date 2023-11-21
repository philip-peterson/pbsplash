/*
 * modeset - DRM Double-Buffered Modesetting Example
 *
 * Written 2012 by David Rheinsberg <david.rheinsberg@gmail.com>
 * Dedicated to the Public Domain.
 */

/*
 * DRM Double-Buffered Modesetting Howto
 * This example extends the modeset.c howto and introduces double-buffering.
 * When drawing a new frame into a framebuffer, we should always draw into an
 * unused buffer and not into the front buffer. If we draw into the front
 * buffer, we might have drawn half the frame when the display-controller starts
 * scanning out the next frame. Hence, we see flickering on the screen.
 * The technique to avoid this is called double-buffering. We have two
 * framebuffers, the front buffer which is currently used for scanout and a
 * back-buffer that is used for drawing operations. When a frame is done, we
 * simply swap both buffers.
 * Swapping does not mean copying data, instead, only the pointers to the
 * buffers are swapped.
 *
 * Please read modeset.c before reading this file as most of the functions stay
 * the same. Only the differences are highlighted here.
 * Also note that triple-buffering or any other number of buffers can be easily
 * implemented by following the scheme here. However, in this example we limit
 * the number of buffers to 2 so it is easier to follow.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "framebuffer.h"

struct modeset_buf;
struct drm_framebuffer;
static int modeset_find_crtc(int fd, drmModeRes *res, drmModeConnector *conn,
			     struct drm_framebuffer *dev);
static int modeset_create_fb(int fd, struct modeset_buf *buf);
static void modeset_destroy_fb(int fd, struct modeset_buf *buf);
static int modeset_setup_dev(int fd, drmModeRes *res, drmModeConnector *conn,
			     struct drm_framebuffer *dev);
static int drm_init(int fd);

/*
 * modeset_open() stays the same as before.
 */

static int drm_open(int *handle, const char *node)
{
	int fd, ret;
	uint64_t has_dumb;

	fd = open(node, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		ret = -errno;
		fprintf(stderr, "cannot open '%s': %m\n", node);
		return ret;
	}

	if (drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0 ||
	    !has_dumb) {
		fprintf(stderr, "drm device '%s' does not support dumb buffers\n",
			node);
		close(fd);
		return -EOPNOTSUPP;
	}

	ret = drm_init(fd);
	if (ret) {
		close(fd);
		return ret;
	}

	*handle = fd;
	return 0;
}

/*
 * Previously, we used the modeset_dev objects to hold buffer informations, too.
 * Technically, we could have split them but avoided this to make the
 * example simpler.
 * However, in this example we need 2 buffers. One back buffer and one front
 * buffer. So we introduce a new structure modeset_buf which contains everything
 * related to a single buffer. Each device now gets an array of two of these
 * buffers.
 * Each buffer consists of width, height, stride, size, handle, map and fb-id.
 * They have the same meaning as before.
 *
 * Each device also gets a new integer field: front_buf. This field contains the
 * index of the buffer that is currently used as front buffer / scanout buffer.
 * In our example it can be 0 or 1. We flip it by using XOR:
 *   dev->front_buf ^= dev->front_buf
 *
 * Everything else stays the same.
 */

struct drm_framebuffer *drm = NULL;

/*
 * modeset_prepare() stays the same.
 */

static int drm_init(int fd)
{
	drmModeRes *res;
	drmModeConnector *conn;
	unsigned int i;
	struct drm_framebuffer *dev;
	struct modeset_buf *buf;
	int ret;

	/* retrieve resources */
	res = drmModeGetResources(fd);
	if (!res) {
		fprintf(stderr, "cannot retrieve DRM resources (%d): %m\n",
			errno);
		return -errno;
	}

	/* iterate all connectors */
	for (i = 0; i < res->count_connectors; ++i) {
		/* get information for each connector */
		conn = drmModeGetConnector(fd, res->connectors[i]);
		if (!conn) {
			fprintf(stderr, "cannot retrieve DRM connector %u:%u (%d): %m\n",
				i, res->connectors[i], errno);
			continue;
		}

		/* create a device structure */
		dev = malloc(sizeof(*dev));
		memset(dev, 0, sizeof(*dev));
		dev->conn = conn->connector_id;
		dev->mm_width = conn->mmWidth;
		dev->mm_height = conn->mmHeight;

		/* call helper function to prepare this connector */
		ret = modeset_setup_dev(fd, res, conn, dev);
		if (ret) {
			if (ret != -ENOENT) {
				errno = -ret;
				fprintf(stderr, "cannot setup device for connector %u:%u (%d): %m\n",
					i, res->connectors[i], errno);
			}
			free(dev);
			drmModeFreeConnector(conn);
			continue;
		}

		/* free connector data and link device into global list */
		drmModeFreeConnector(conn);
		drm = dev;
		break;
	}

	/* free resources again */
	drmModeFreeResources(res);

	/* perform actual modesetting on each found connector+CRTC */
	dev->saved_crtc = drmModeGetCrtc(fd, dev->crtc);
	buf = &dev->bufs[dev->front_buf];
	ret = drmModeSetCrtc(fd, dev->crtc, buf->fb, 0, 0,
				&dev->conn, 1, &dev->mode);
	if (ret)
		fprintf(stderr, "cannot set CRTC for connector %u (%d): %m\n",
			dev->conn, errno);

	return 0;
}

/*
 * modeset_setup_dev() sets up all resources for a single device. It mostly
 * stays the same, but one thing changes: We allocate two framebuffers instead
 * of one. That is, we call modeset_create_fb() twice.
 * We also copy the width/height information into both framebuffers so
 * modeset_create_fb() can use them without requiring a pointer to modeset_dev.
 */

static int modeset_setup_dev(int fd, drmModeRes *res, drmModeConnector *conn,
			     struct drm_framebuffer *dev)
{
	int ret;

	/* check if a monitor is connected */
	if (conn->connection != DRM_MODE_CONNECTED) {
		fprintf(stderr, "ignoring unused connector %u\n",
			conn->connector_id);
		return -ENOENT;
	}

	/* check if there is at least one valid mode */
	if (conn->count_modes == 0) {
		fprintf(stderr, "no valid mode for connector %u\n",
			conn->connector_id);
		return -EFAULT;
	}

	/* copy the mode information into our device structure and into both
	 * buffers */
	memcpy(&dev->mode, &conn->modes[0], sizeof(dev->mode));
	dev->bufs[0].width = conn->modes[0].hdisplay;
	dev->bufs[0].height = conn->modes[0].vdisplay;
	dev->bufs[1].width = conn->modes[0].hdisplay;
	dev->bufs[1].height = conn->modes[0].vdisplay;
	fprintf(stderr, "mode for connector %u is %ux%u\n",
		conn->connector_id, dev->bufs[0].width, dev->bufs[0].height);

	/* find a crtc for this connector */
	ret = modeset_find_crtc(fd, res, conn, dev);
	if (ret) {
		fprintf(stderr, "no valid crtc for connector %u\n",
			conn->connector_id);
		return ret;
	}

	/* create framebuffer #1 for this CRTC */
	ret = modeset_create_fb(fd, &dev->bufs[0]);
	if (ret) {
		fprintf(stderr, "cannot create framebuffer for connector %u\n",
			conn->connector_id);
		return ret;
	}

	/* create framebuffer #2 for this CRTC */
	ret = modeset_create_fb(fd, &dev->bufs[1]);
	if (ret) {
		fprintf(stderr, "cannot create framebuffer for connector %u\n",
			conn->connector_id);
		modeset_destroy_fb(fd, &dev->bufs[0]);
		return ret;
	}

	return 0;
}

/*
 * modeset_find_crtc() stays the same.
 */

static int modeset_find_crtc(int fd, drmModeRes *res, drmModeConnector *conn,
			     struct drm_framebuffer *dev)
{
	drmModeEncoder *enc;
	unsigned int i, j;
	int32_t crtc;

	/* first try the currently conected encoder+crtc */
	if (conn->encoder_id)
		enc = drmModeGetEncoder(fd, conn->encoder_id);
	else
		enc = NULL;

	if (enc) {
		if (enc->crtc_id) {
			crtc = enc->crtc_id;
			if (crtc >= 0) {
				drmModeFreeEncoder(enc);
				dev->crtc = crtc;
				return 0;
			}
		}

		drmModeFreeEncoder(enc);
	}

	/* If the connector is not currently bound to an encoder or if the
	 * encoder+crtc is already used by another connector (actually unlikely
	 * but lets be safe), iterate all other available encoders to find a
	 * matching CRTC. */
	for (i = 0; i < conn->count_encoders; ++i) {
		enc = drmModeGetEncoder(fd, conn->encoders[i]);
		if (!enc) {
			fprintf(stderr, "cannot retrieve encoder %u:%u (%d): %m\n",
				i, conn->encoders[i], errno);
			continue;
		}

		/* iterate all global CRTCs */
		for (j = 0; j < res->count_crtcs; ++j) {
			/* check whether this CRTC works with the encoder */
			if (!(enc->possible_crtcs & (1 << j)))
				continue;

			/* check that no other device already uses this CRTC */
			crtc = res->crtcs[j];

			/* we have found a CRTC, so save it and return */
			if (crtc >= 0) {
				drmModeFreeEncoder(enc);
				dev->crtc = crtc;
				return 0;
			}
		}

		drmModeFreeEncoder(enc);
	}

	fprintf(stderr, "cannot find suitable CRTC for connector %u\n",
		conn->connector_id);
	return -ENOENT;
}

/*
 * modeset_create_fb() is mostly the same as before. Buf instead of writing the
 * fields of a modeset_dev, we now require a buffer pointer passed as @buf.
 * Please note that buf->width and buf->height are initialized by
 * modeset_setup_dev() so we can use them here.
 */

static int modeset_create_fb(int fd, struct modeset_buf *buf)
{
	struct drm_mode_create_dumb creq;
	struct drm_mode_destroy_dumb dreq;
	struct drm_mode_map_dumb mreq;
	int ret;

	/* create dumb buffer */
	memset(&creq, 0, sizeof(creq));
	creq.width = buf->width;
	creq.height = buf->height;
	creq.bpp = 32;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
	if (ret < 0) {
		fprintf(stderr, "cannot create dumb buffer (%d): %m\n",
			errno);
		return -errno;
	}
	buf->pitch = creq.pitch;
	buf->size = creq.size;
	buf->handle = creq.handle;

	/* create framebuffer object for the dumb-buffer */
	ret = drmModeAddFB(fd, buf->width, buf->height, 24, 32, buf->pitch,
			   buf->handle, &buf->fb);
	if (ret) {
		fprintf(stderr, "cannot create framebuffer (%d): %m\n",
			errno);
		ret = -errno;
		goto err_destroy;
	}

	/* prepare buffer for memory mapping */
	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = buf->handle;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
	if (ret) {
		fprintf(stderr, "cannot map dumb buffer (%d): %m\n",
			errno);
		ret = -errno;
		goto err_fb;
	}

	/* perform actual memory mapping */
	buf->map = mmap(0, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		        fd, mreq.offset);
	if (buf->map == MAP_FAILED) {
		fprintf(stderr, "cannot mmap dumb buffer (%d): %m\n",
			errno);
		ret = -errno;
		goto err_fb;
	}

	/* clear the framebuffer to 0 */
	memset(buf->map, 0, buf->size);

	return 0;

err_fb:
	drmModeRmFB(fd, buf->fb);
err_destroy:
	memset(&dreq, 0, sizeof(dreq));
	dreq.handle = buf->handle;
	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
	return ret;
}

/*
 * modeset_destroy_fb() is a new function. It does exactly the reverse of
 * modeset_create_fb() and destroys a single framebuffer. The modeset.c example
 * used to do this directly in modeset_cleanup().
 * We simply unmap the buffer, remove the drm-FB and destroy the memory buffer.
 */

static void modeset_destroy_fb(int fd, struct modeset_buf *buf)
{
	struct drm_mode_destroy_dumb dreq;

	/* unmap buffer */
	munmap(buf->map, buf->size);

	/* delete framebuffer */
	drmModeRmFB(fd, buf->fb);

	/* delete dumb buffer */
	memset(&dreq, 0, sizeof(dreq));
	dreq.handle = buf->handle;
	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
}

int drm_framebuffer_init(int *handle, const char *card)
{
	int ret;

	card = card ?: "/dev/dri/card0";

	printf("using card '%s'\n", card);

	/* open the DRM device */
	ret = drm_open(handle, card);
	if (ret) {
		errno = -ret;
		fprintf(stderr, "drm_open failed with error %d: %m\n", errno);
		return ret;
	}

	printf("DRM mode %dx%d @ %dHz\n", drm->bufs[0].width, drm->bufs[0].height,
	       drm->mode.vrefresh);

	/* draw some colors for 5seconds */
	// modeset_draw(*handle);

	// /* cleanup everything */
	// drm_framebuffer_close(*handle);

	return ret;
}

/*
 * modeset_cleanup() stays the same as before. But it now calls
 * modeset_destroy_fb() instead of accessing the framebuffers directly.
 */

void drm_framebuffer_close(int handle)
{
	/* restore saved CRTC configuration */
	drmModeSetCrtc(handle,
			drm->saved_crtc->crtc_id,
			drm->saved_crtc->buffer_id,
			drm->saved_crtc->x,
			drm->saved_crtc->y,
			&drm->conn,
			1,
			&drm->saved_crtc->mode);
	drmModeFreeCrtc(drm->saved_crtc);

	/* destroy framebuffers */
	modeset_destroy_fb(handle, &drm->bufs[1]);
	modeset_destroy_fb(handle, &drm->bufs[0]);

	/* free allocated memory */
	free(drm);
}

/*
 * This was a very short extension to the basic modesetting example that shows
 * how double-buffering is implemented. Double-buffering is the de-facto
 * standard in any graphics application so any other example will be based on
 * this. It is important to understand the ideas behind it as the code is pretty
 * easy and short compared to modeset.c.
 *
 * Double-buffering doesn't solve all problems. Vsync'ed page-flips solve most
 * of the problems that still occur, but has problems on it's own (see
 * modeset-vsync.c for a discussion).
 *
 * If you want more code, I can recommend reading the source-code of:
 *  - plymouth (which uses dumb-buffers like this example; very easy to understand)
 *  - kmscon (which uses libuterm to do this)
 *  - wayland (very sophisticated DRM renderer; hard to understand fully as it
 *             uses more complicated techniques like DRM planes)
 *  - xserver (very hard to understand as it is split across many files/projects)
 *
 * Any feedback is welcome. Feel free to use this code freely for your own
 * documentation or projects.
 *
 *  - Hosted on http://github.com/dvdhrm/docs
 *  - Written by David Rheinsberg <david.rheinsberg@gmail.com>
 */