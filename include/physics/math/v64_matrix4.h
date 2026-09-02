#ifndef VOLCANO_64_MATRIX4_H
#define VOLCANO_64_MATRIX4_H

#include <fgeom.h>
#include <mgfx.h>

#include "physics/math/v64_vector3.h"
#include "physics/math/v64_quaternion.h"


/* Row-major 4x4 float matrix, libdragon's fgeom type underneath so it can be
 * handed to magma/mgfx as-is. Rows 0-2 are the basis vectors, row 3 the
 * translation. */
typedef fm_mat4_t Matrix4;


void matrix4_setIdentity(Matrix4 *m);

void matrix4_fromSrt(Matrix4 *m, const Vector3 *scale, const Quaternion *rotation, const Vector3 *translation);
void matrix4_fromSrtEuler(Matrix4 *m, const Vector3 *scale, const Vector3 *rotation, const Vector3 *translation);

void matrix4_product(Matrix4 *out, const Matrix4 *a, const Matrix4 *b);

/* View matrix from camera eye, target and up (right-handed, -Z forward). */
void matrix4_lookAt(Matrix4 *m, const Vector3 *eye, const Vector3 *target, const Vector3 *up);

/* Conversion to the 16.16 fixed-point layout the RSP reads (magma uniform
 * format: all integer parts first, then all fractional parts). */
void matrix4_toFixed(mgfx_matrix_t *out, const Matrix4 *m);

/* Writes the identity directly in fixed point. */
void matrix4_setFixedIdentity(mgfx_matrix_t *m);


#endif
