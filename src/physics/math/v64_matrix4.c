#include "physics/math/v64_matrix4.h"


void matrix4_setIdentity(Matrix4 *m)
{
	fm_mat4_identity(m);
}

void matrix4_fromSrt(Matrix4 *m, const Vector3 *scale, const Quaternion *rotation, const Vector3 *translation)
{
	fm_vec3_t s = {{scale->x, scale->y, scale->z}};
	fm_quat_t r = {{rotation->x, rotation->y, rotation->z, rotation->w}};
	fm_vec3_t t = {{translation->x, translation->y, translation->z}};

	fm_mat4_from_srt(m, &s, &r, &t);
}

void matrix4_fromSrtEuler(Matrix4 *m, const Vector3 *scale, const Vector3 *rotation, const Vector3 *translation)
{
	fm_vec3_t s = {{scale->x, scale->y, scale->z}};
	float     r[3] = {rotation->x, rotation->y, rotation->z};
	fm_vec3_t t = {{translation->x, translation->y, translation->z}};

	fm_mat4_from_srt_euler(m, &s, r, &t);
}

void matrix4_product(Matrix4 *out, const Matrix4 *a, const Matrix4 *b)
{
	fm_mat4_mul(out, a, b);
}

/* Ported from tiny3d's t3d_mat4_look_at (Max Bebök, MIT, see LICENSE). */
void matrix4_lookAt(Matrix4 *m, const Vector3 *eye, const Vector3 *target, const Vector3 *up)
{
	Vector3 forward = vector3_difference(target, eye);
	vector3_normalize(&forward);

	Vector3 side = vector3_cross(&forward, up);
	vector3_normalize(&side);

	Vector3 upCalc = vector3_cross(&side, &forward);

	float dotSide = -vector3_dot(&side, eye);
	float dotUp   = -vector3_dot(&upCalc, eye);
	float dotFwd  =  vector3_dot(&forward, eye);

	*m = (Matrix4){{
		{side.x, upCalc.x, -forward.x, 0.0f},
		{side.y, upCalc.y, -forward.y, 0.0f},
		{side.z, upCalc.z, -forward.z, 0.0f},
		{dotSide, dotUp,    dotFwd,    1.0f}
	}};
}

void matrix4_setFixedIdentity(mgfx_matrix_t *m)
{
	*m = (mgfx_matrix_t){
		.i = {1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1},
		.f = {0},
	};
}

/* Ported from tiny3d's t3d_mat4_to_fixed (Max Bebök, MIT), writing magma's
 * uniform layout instead of the tiny3d ucode's. Same 16.16 conversion as
 * mgfx uses internally; the unrolled 64-bit stores are tiny3d's trick, valid
 * here because each row's four int (and frac) parts stay contiguous and the
 * matrix is 16-byte aligned. */
void matrix4_toFixed(mgfx_matrix_t *out, const Matrix4 *m)
{
	for (uint32_t y = 0; y < 4; ++y) {
		uint32_t fixed0 = (uint32_t)(int32_t)(m->m[y][0] * 65536.f);
		uint32_t fixed1 = (uint32_t)(int32_t)(m->m[y][1] * 65536.f);
		uint32_t fixed2 = (uint32_t)(int32_t)(m->m[y][2] * 65536.f);
		uint32_t fixed3 = (uint32_t)(int32_t)(m->m[y][3] * 65536.f);

		uint64_t I = (fixed0 & 0xFFFF0000) | (fixed1 >> 16);
		I <<= 32; /* needs to be separate, otherwise -Os generates wrong code */
		I |= (fixed2 & 0xFFFF0000) | (fixed3 >> 16);

		uint64_t F = (fixed0 << 16) | (fixed1 & 0xFFFF);
		F <<= 32; /* needs to be separate, otherwise -Os generates wrong code */
		F |= (fixed2 << 16) | (fixed3 & 0xFFFF);

		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wstrict-aliasing"
			*(uint64_t*)&out->i[y * 4] = I; /* guaranteed to be 64-bit aligned */
			*(uint64_t*)&out->f[y * 4] = F;
		#pragma GCC diagnostic pop
	}
}
