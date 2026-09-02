#ifndef VOLCANO_64_CAMERA_H
#define VOLCANO_64_CAMERA_H

#include <stdbool.h>

#include "physics/math/v64_vector3.h"
#include "v64_spring_arm.h"


typedef enum {

	CAMERA_TYPE_SPRING_ARM,
	CAMERA_TYPE_COUNT,
	CAMERA_TYPE_NONE,

} CameraType;


typedef struct {

	CameraType type;

	float field_of_view;
	float near_clipping;
	float far_clipping;

	/* Refits near/far every frame to the boxes the frustum sees: nearest
	   visible corner in, farthest out — the depth-range fit shadow cascades
	   use. Off keeps the fixed planes above; they also stand in while
	   nothing is visible. */
	bool auto_clipping;

	union {
		CameraSpringArmDef spring_arm;
	};

} CameraDef;


/* Sane lens bounds, in degrees. Below the minimum the projection's
   cotangent blows past what the RSP's 16.16 matrices can hold (it crashed
   the fixed-point cast); above the maximum the image is unusable anyway. */
#define CAMERA_FOV_MIN  15.0f
#define CAMERA_FOV_MAX 120.0f

typedef struct Camera {

	Vector3 position;
	Vector3 target;

	/* Same split as the arm: the stick writes the target, the aim adds its
	   offset, and the lens chases the sum. */
	float target_field_of_view;
	float field_of_view;
	float near_clipping;
	float far_clipping;

	/* The def's planes, kept apart: the clipping method moves the live
	   ones starting from these. */
	float base_near_clipping;
	float base_far_clipping;

	bool  auto_clipping;   /* the clipping method refits the planes each frame */

	/* view target transition: the outgoing center is frozen at switch time, so
	   the old target moving afterwards cannot disturb the blend */
	Vector3 blend_from;
	float   blend_elapsed;
	float   blend_duration;

	CameraType  type;

	union {
		struct {
			CameraSpringArmSettings settings;
			CameraSpringArmData     data;
		} spring_arm;
	};

} Camera;


struct Scene3D;

void camera_init(Camera *camera);
void camera_reset(Camera *camera);
void camera_update(Camera *camera, Vector3 *center, const struct Scene3D *scene, float dt);
void camera_setViewTarget(Camera *camera, const Vector3 *from, float duration);
float camera_getAngleAround(const Camera *camera, const Vector3 *point);
float camera_getPitch(const Camera *camera);
Vector3 camera_getRight(const Camera *camera);
void camera_fitClipping(Camera *camera, const struct Scene3D *scene);

#endif
