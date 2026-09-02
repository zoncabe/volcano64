#include <libdragon.h>
#include <magma.h>

#include "time/v64_time.h"
#include "sound/v64_sound.h"
#include "camera/v64_camera.h"
#include "physics/math/v64_math_common.h"
#include "viewport/v64_viewport.h"


const resolution_t WIDESCREEN = {.width = 424, .height = 240, .interlaced = INTERLACE_OFF};
static Viewport viewport;


Viewport* viewport_get(void) { return &viewport; }

/* View, view-projection and frustum for the frame, from whatever projection
   is set. The ucode has no matrix stack: every draw later multiplies its
   model matrix against view_projection on the CPU. */
static void viewport_composeCamera(void)
{
	matrix4_lookAt(&viewport.view,
		&viewport.camera.position,
		&viewport.camera.target,
		&(Vector3){0.0f, 0.0f, 1.0f});

	matrix4_product(&viewport.view_projection, &viewport.projection, &viewport.view);
	frustum_fromMatrix4(&viewport.frustum, &viewport.view_projection);

	/* The ucode normalizes perspective with the clipping planes (what t3d
	   called w-normalize); they ride the viewport and follow the camera,
	   auto-clipping included. */
	viewport.screen.z_near = viewport.camera.near_clipping;
	viewport.screen.z_far  = viewport.camera.far_clipping;
}

void viewport_setPerspectiveCamera()
{
	float aspect = (float)display_get_width() / (float)display_get_height();

	mg_mat4_perspective(&viewport.projection,
		deg_to_rad(viewport.camera.field_of_view),
		aspect,
		viewport.camera.near_clipping,
		viewport.camera.far_clipping);

	viewport_composeCamera();
}

/* 'size' is half of the visible height, in render units; the width follows
   from the display aspect. */
void viewport_setIsometricCamera(float size)
{
	float aspect = (float)display_get_width() / (float)display_get_height();
	float half_w = size * aspect;

	mg_mat4_ortho(&viewport.projection,
		-half_w, half_w,
		-size, size,
		viewport.camera.near_clipping,
		viewport.camera.far_clipping);

	viewport_composeCamera();
}

void viewport_init()
{
	display_init(RESOLUTION_320x240, DEPTH_16_BPP, FB_COUNT, GAMMA_NONE, FILTERS_DISABLED);
	mg_init();

	viewport.screen = (mg_viewport_t){
		.x = 0,
		.y = 0,
		.width = display_get_width(),
		.height = display_get_height(),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};

	camera_init(&viewport.camera);
	viewport.fb_index = 0;
}

void viewport_clear(color_t color)
{
	rdpq_attach(display_get(), display_get_zbuf());
	rdpq_clear(color);
	rdpq_clear_z(ZBUF_MAX);
}

void viewport_updateCamera(Vector3 *center, const struct Scene3D *scene)
{
	camera_update(&viewport.camera, center, scene, time_get()->delta);
}
