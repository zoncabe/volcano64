#include <math.h>

#include "physics/math/v64_vector2.h"


float vector2_magnitude(const Vector2 *v)
{
	return sqrtf(v->x * v->x + v->y * v->y);
}
