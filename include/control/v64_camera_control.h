#ifndef VOLCANO_64_CAMERA_CONTROL_H
#define VOLCANO_64_CAMERA_CONTROL_H

#include "v64_controller.h"
#include "camera/v64_camera.h"

typedef struct CameraControlBinding {

	PlayerID player;

	ButtonID pan_left;
	ButtonID pan_right;
	ButtonID tilt_up;
	ButtonID tilt_down;

	ButtonID distance_in;
	ButtonID distance_out;
	ButtonID fov_in;
	ButtonID fov_out;

} CameraControlBinding;


/* Reads the controller of the player the binding names. The scene rides
   through to the camera update, for the clipping fit. */
void cameraControl_update(Camera *camera, const CameraControlBinding *binding,
                          const struct Scene3D *scene, float dt);
void cameraControl_setDistance(Camera *camera, float distance, float dt);
void cameraControl_setFieldOfView(Camera *camera, float field_of_view, float dt);
void cameraControl_setSideOffset(Camera *camera, float side_offset, float dt);


#endif
