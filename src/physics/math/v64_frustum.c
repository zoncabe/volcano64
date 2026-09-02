/* Ported from tiny3d's t3dmath frustum functions (Max Bebök, MIT, see
 * LICENSE), rewired to the engine's math types. */
#include <math.h>

#include "physics/math/v64_frustum.h"


void frustum_fromMatrix4(Frustum *frustum, const Matrix4 *m)
{
	for (int i = 0; i < 4; ++i) {
		frustum->planes[0].v[i] = m->m[i][3] + m->m[i][0]; /* left   */
		frustum->planes[1].v[i] = m->m[i][3] - m->m[i][0]; /* right  */
		frustum->planes[2].v[i] = m->m[i][3] + m->m[i][1]; /* bottom */
		frustum->planes[3].v[i] = m->m[i][3] - m->m[i][1]; /* top    */
		frustum->planes[4].v[i] = m->m[i][3] + m->m[i][2]; /* near   */
		frustum->planes[5].v[i] = m->m[i][3] - m->m[i][2]; /* far    */
	}
	for (int i = 0; i < 6; ++i) {
		fm_vec4_t *p = &frustum->planes[i];
		float len = sqrtf(p->v[0]*p->v[0] + p->v[1]*p->v[1] + p->v[2]*p->v[2]);
		p->v[0] /= len;
		p->v[1] /= len;
		p->v[2] /= len;
		p->v[3] /= len;
	}
}

void frustum_scale(Frustum *frustum, float scale)
{
	for (int i = 0; i < 6; ++i) {
		frustum->planes[i].v[0] *= scale;
		frustum->planes[i].v[1] *= scale;
		frustum->planes[i].v[2] *= scale;
	}
}

bool frustum_vsAabb(const Frustum *frustum, const Vector3 *min, const Vector3 *max)
{
	for (int i = 0; i < 6; ++i) {
		float p0Min = frustum->planes[i].v[0] * min->x;
		float p0Max = frustum->planes[i].v[0] * max->x;
		float p1Min = frustum->planes[i].v[1] * min->y;
		float p1Max = frustum->planes[i].v[1] * max->y;

		float p2MinAndW = -frustum->planes[i].v[3] - frustum->planes[i].v[2] * min->z;
		if (p0Min + p1Min > p2MinAndW) continue;
		if (p0Max + p1Min > p2MinAndW) continue;
		if (p0Min + p1Max > p2MinAndW) continue;
		if (p0Max + p1Max > p2MinAndW) continue;

		float p2MaxAndW = -frustum->planes[i].v[3] - frustum->planes[i].v[2] * max->z;
		if (p0Min + p1Min > p2MaxAndW) continue;
		if (p0Max + p1Min > p2MaxAndW) continue;
		if (p0Min + p1Max > p2MaxAndW) continue;
		if (p0Max + p1Max > p2MaxAndW) continue;
		return false;
	}
	return true;
}

bool frustum_vsAabbS16(const Frustum *frustum, const int16_t min[3], const int16_t max[3])
{
	for (int i = 0; i < 6; ++i) {
		float p0Min = frustum->planes[i].v[0] * min[0];
		float p0Max = frustum->planes[i].v[0] * max[0];
		float p1Min = frustum->planes[i].v[1] * min[1];
		float p1Max = frustum->planes[i].v[1] * max[1];

		float p2MinAndW = -frustum->planes[i].v[3] - frustum->planes[i].v[2] * min[2];
		if (p0Min + p1Min > p2MinAndW) continue;
		if (p0Max + p1Min > p2MinAndW) continue;
		if (p0Min + p1Max > p2MinAndW) continue;
		if (p0Max + p1Max > p2MinAndW) continue;

		float p2MaxAndW = -frustum->planes[i].v[3] - frustum->planes[i].v[2] * max[2];
		if (p0Min + p1Min > p2MaxAndW) continue;
		if (p0Max + p1Min > p2MaxAndW) continue;
		if (p0Min + p1Max > p2MaxAndW) continue;
		if (p0Max + p1Max > p2MaxAndW) continue;
		return false;
	}
	return true;
}

bool frustum_vsSphere(const Frustum *frustum, const Vector3 *center, float radius)
{
	for (int i = 0; i < 6; ++i) {
		const fm_vec4_t *p = &frustum->planes[i];
		float dist = p->v[0]*center->x + p->v[1]*center->y + p->v[2]*center->z + p->v[3];
		if (dist < -radius) return false;
	}
	return true;
}
