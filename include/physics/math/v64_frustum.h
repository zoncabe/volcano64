#ifndef VOLCANO_64_FRUSTUM_H
#define VOLCANO_64_FRUSTUM_H

#include <fgeom.h>

#include "physics/math/v64_vector3.h"
#include "physics/math/v64_matrix4.h"


/* View frustum as six planes, extracted from a view-projection matrix. */
typedef struct Frustum {
	fm_vec4_t planes[6];
} Frustum;


void frustum_fromMatrix4(Frustum *frustum, const Matrix4 *m);
void frustum_scale(Frustum *frustum, float scale);

bool frustum_vsAabb(const Frustum *frustum, const Vector3 *min, const Vector3 *max);
bool frustum_vsAabbS16(const Frustum *frustum, const int16_t min[3], const int16_t max[3]);
bool frustum_vsSphere(const Frustum *frustum, const Vector3 *center, float radius);


#endif
