#ifndef VOLCANO_64_VIEWPORT_H
#define VOLCANO_64_VIEWPORT_H

#include <libdragon.h>
#include <magma.h>

#include "camera/v64_camera.h"
#include "physics/math/v64_math.h"
#include "physics/math/v64_frustum.h"

#define FB_COUNT 3


typedef struct Viewport {

	Camera camera;
	int fb_index;

	/* Camera matrices for the frame. The magma ucode has no matrix stack:
	   each draw gets its final matrices computed on the CPU from these. */
	Matrix4 projection;
	Matrix4 view;
	Matrix4 view_projection;
	Frustum frustum;

	mg_viewport_t screen;

} Viewport;

Viewport *viewport_get(void);


void viewport_init(void);
void viewport_clear(color_t color);
void viewport_updateCamera(Vector3 *center, const struct Scene3D *scene);

/* The projection is the game's call, so it picks one and keeps it fed.
   'size' is half of the visible height, in render units; the width follows
   from the display aspect. */
void viewport_setPerspectiveCamera(void);
void viewport_setIsometricCamera(float size);

#endif
