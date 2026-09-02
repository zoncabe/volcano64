#ifndef VOLCANO_64_MESH_H
#define VOLCANO_64_MESH_H

#include <libdragon.h>
#include "physics/math/v64_math.h"
#include "animation/v64_model.h"
#include "animation/v64_armature.h"

#include "render/v64_render.h"
#include "physics/math/v64_vector3.h"
#include "physics/math/v64_quaternion.h"


struct MeshDeform;

typedef struct {
	Vector3 min, max;
} MeshBound;

typedef struct {
	rspq_block_t **dl;            /* one block per part */
	uint8_t        dl_count;
	uint8_t        visible;       /* bitmask: parts to render */
	Matrix4       *matrix_buffer; /* model matrix per fb; NULL = identity baked in dl */
	Model      *model;
	Armature   *skeleton;      /* NULL = static mesh (set by character_create) */

	/* Where the vertices come from when something else drives them. The
	   binding lives in its own module, so the mesh only needs to know it is
	   there. NULL = vertices come straight from the model. */
	struct MeshDeform *deform;

	/* Handed to the per-frame material setup when the mesh draws through the
	   object path; lets the owner scroll tiles or swap textures. NULL for
	   everything that has no business there. */
	ModelDrawConf *draw_conf;

	/* Model-space box of the whole mesh, taken once. */
	int16_t local_min[3];
	int16_t local_max[3];

	/* World box per model object, rebuilt with the matrix. Index 0 is the
	   whole mesh. */
	MeshBound *bound;
	uint8_t    bound_count;
	bool       culled;
} Mesh;


void mesh_initBounds(Mesh *mesh);

/* Tests the world boxes against the frustum and writes the mesh's own
   visibility: culled for the whole mesh, isVisible per model object for
   meshes drawn through the object path. */
void mesh_cull(Mesh *mesh, const Viewport *viewport);

void mesh_setMatrix(Mesh *mesh, const RenderTransform *transform, uint8_t fb_index);

/* Same, but from a simulated body: position in metres and a quaternion, which
   is what a tumbling body actually has. */
void mesh_setMatrixFromBody(Mesh *mesh, const Vector3 *position, const Quaternion *rotation,
                            const Vector3 *scale, uint8_t fb_index);

/* Records part 0 (every object not in the list) plus one part per named
   object, in list order. Skinned models get their bone palette at draw time,
   not here; pass NULL as matrices for static ones. */
void mesh_recordParts(Mesh *mesh, const char *const *names, uint8_t count, const mgfx_matrix_t *matrices);

/* Records one block per model object, in the object's own userBlock. */
void mesh_recordObjects(Mesh *mesh);

/* Hands the vertices over to an external set of points, matched by rest
   position. `scale` converts source units to render units. Pass source_normal
   to have the shading follow the deformation, and source_rgba to drive the
   vertex colors too; NULL keeps the model's own. */
bool mesh_setDeform(Mesh *mesh, const Vector3 *source, const Vector3 *source_normal,
                    const uint8_t *source_rgba, uint16_t source_count, float scale);

/* Pushes the current source positions and normals into the vertex buffer.
   No-op when the mesh is not deformed. */
void mesh_updateDeform(Mesh *mesh, uint8_t fb_index);

/* Points the deformed mesh at this frame's vertices. Call right before its
   display list runs. No-op when the mesh is not deformed. */
void mesh_bindDeformFrame(Mesh *mesh, uint8_t fb_index);

#endif
