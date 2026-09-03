
#include <assert.h>
#include <malloc.h>
#include <string.h>
#include "animation/v64_model.h"

#include "graphics/v64_mesh.h"
#include "shaders/v64_mesh_deform.h"
#include "physics/math/v64_math_common.h"
#include "physics/math/v64_quaternion.h"


/* A skinned model's boxes are written per bone, so none of them says where the
   mesh ends up. Two things bound that for good: a vertex never leaves its own
   bone by more than the model box reaches, and a bone never leaves the root by
   more than its chain is long. The box that holds both holds every pose the
   rig can take, so it is measured once and never again. */
static void mesh_skinnedBound(Mesh *mesh, const SkeletonData *skeleton)
{
	float reach = 0.0f;
	for (int i = 0; i < 3; i++) {
		float lo = -mesh->model->aabbMin[i];
		float hi =  mesh->model->aabbMax[i];
		if (lo > reach) reach = lo;
		if (hi > reach) reach = hi;
	}

	float chain[skeleton->boneCount];
	float longest = 0.0f;

	for (int b = 0; b < skeleton->boneCount; b++) {
		const BoneData *bone = &skeleton->bones[b];

		chain[b] = vector3_magnitude(&bone->position);
		if (bone->parentIdx < b) chain[b] += chain[bone->parentIdx];
		if (chain[b] > longest) longest = chain[b];
	}

	int16_t half = (int16_t)(longest + reach);
	for (int i = 0; i < 3; i++) {
		mesh->local_min[i] = -half;
		mesh->local_max[i] =  half;
	}
}

void mesh_initBounds(Mesh *mesh)
{
	uint8_t count = 0;
	ModelIter it = model_iterCreate(mesh->model, CHUNK_TYPE_OBJECT);
	while (model_iterNext(&it)) count++;

	mesh->bound_count = 1 + count;
	mesh->bound = calloc(mesh->bound_count, sizeof(MeshBound));
	assert(mesh->bound);
	mesh->culled = false;

	const SkeletonData *skeleton = model_getSkeleton(mesh->model);
	if (skeleton) {
		mesh_skinnedBound(mesh, skeleton);
		return;
	}

	for (int i = 0; i < 3; i++) {
		mesh->local_min[i] = mesh->model->aabbMin[i];
		mesh->local_max[i] = mesh->model->aabbMax[i];
	}
}

/* Places a model-space box in the world without walking its corners: the
   centre goes through the matrix, and the half-extent through its absolute
   value, which is the axis-aligned box that still contains it after any
   rotation. */
static void mesh_placeBound(MeshBound *bound, const int16_t *min, const int16_t *max, const Matrix4 *m)
{
	float centre[3], extent[3];
	for (int i = 0; i < 3; i++) {
		centre[i] = (max[i] + min[i]) * 0.5f;
		extent[i] = (max[i] - min[i]) * 0.5f;
	}

	for (int i = 0; i < 3; i++) {
		float c = m->m[3][i];
		float e = 0.0f;

		for (int k = 0; k < 3; k++) {
			c += centre[k] * m->m[k][i];
			e += extent[k] * fabsf(m->m[k][i]);
		}

		*vector3_component(&bound->min, i) = c - e;
		*vector3_component(&bound->max, i) = c + e;
	}
}

static void mesh_updateBounds(Mesh *mesh, const Matrix4 *matrix)
{
	if (mesh->bound == NULL) return;

	mesh_placeBound(&mesh->bound[0], mesh->local_min, mesh->local_max, matrix);

	uint8_t i = 1;
	ModelIter it = model_iterCreate(mesh->model, CHUNK_TYPE_OBJECT);
	while (model_iterNext(&it) && i < mesh->bound_count)
		mesh_placeBound(&mesh->bound[i++], it.object->aabbMin, it.object->aabbMax, matrix);
}

void mesh_cull(Mesh *mesh, const Viewport *viewport)
{
	mesh->culled = !frustum_vsAabb(&viewport->frustum,
	                                    &mesh->bound[0].min, &mesh->bound[0].max);
	if (mesh->culled) return;

	/* Only the object path draws per object; recorded meshes cut at the
	   whole-mesh box. */
	if (mesh->dl_count != 0) return;

	uint8_t b = 1;
	ModelIter it = model_iterCreate(mesh->model, CHUNK_TYPE_OBJECT);
	while (model_iterNext(&it) && b < mesh->bound_count) {
		it.object->isVisible = frustum_vsAabb(&viewport->frustum,
		                                           &mesh->bound[b].min, &mesh->bound[b].max);
		b++;
	}
}


/* A simulated body tumbles, and euler angles cannot describe that without
   picking an order and losing the tumble at the poles. The body already keeps
   a quaternion, so it goes straight to the matrix. */
void mesh_setMatrixFromBody(Mesh *mesh, const Vector3 *position, const Quaternion *rotation,
                            const Vector3 *scale, uint8_t fb_index)
{
	Matrix4 *matrix = &mesh->matrix_buffer[fb_index];

	matrix4_fromSrt(
		matrix,
		scale,
		rotation,
		&(Vector3){ position->x * RENDER_SCALE, position->y * RENDER_SCALE, position->z * RENDER_SCALE }
	);

	mesh_updateBounds(mesh, matrix);
}


void mesh_setMatrix(Mesh *mesh, const RenderTransform *transform, uint8_t fb_index)
{
	Matrix4 *matrix = &mesh->matrix_buffer[fb_index];

	matrix4_fromSrtEuler(
		matrix,
		&transform->scale,
		&(Vector3){deg_to_rad(transform->rotation.x), deg_to_rad(transform->rotation.y), deg_to_rad(transform->rotation.z)},
		&transform->position
	);

	mesh_updateBounds(mesh, matrix);
}


bool mesh_setDeform(Mesh *mesh, const Vector3 *source, const Vector3 *source_normal,
                    const uint8_t *source_rgba, uint16_t source_count, float scale)
{
	MeshDeform *deform = malloc(sizeof(MeshDeform));
	assert(deform);

	if (!meshDeform_bind(deform, mesh->model, source, source_normal, source_count, scale)) {
		free(deform);
		return false;
	}
	deform->source_rgba = source_rgba;

	/* TODO(magma): the deform module used tiny3d's segment table to swap the
	   vertex buffer per frame under a recorded display list. With magma the
	   swap is a different mg_bind_vertex_buffer before the draw; rewired
	   together with the deform phase. Blocks are re-recorded the same way. */
	if (mesh->dl_count > 0) {
		for (int i = 0; i < mesh->dl_count; i++) rspq_block_free(mesh->dl[i]);

		rspq_block_begin();
		model_draw(mesh->model);
		mesh->dl[0]    = rspq_block_end();
		mesh->dl_count = 1;
	} else {
		/* Object path: the blocks live per object instead. */
		ModelIter it = model_iterCreate(mesh->model, CHUNK_TYPE_OBJECT);
		while (model_iterNext(&it)) {
			if (it.object->userBlock) rspq_block_free(it.object->userBlock);
			rspq_block_begin();
			model_drawObject(it.object, NULL);
			it.object->userBlock = rspq_block_end();
		}
	}

	mesh->deform = deform;
	return true;
}


void mesh_updateDeform(Mesh *mesh, uint8_t fb_index)
{
	if (mesh->deform) meshDeform_apply(mesh->deform, fb_index);
}

/* Separate from the update because it has to run at draw time, in front of the
   display list, not when the vertices are written. */
void mesh_bindDeformFrame(Mesh *mesh, uint8_t fb_index)
{
	if (mesh->deform) meshDeform_bindFrame(mesh->deform, fb_index);
}


/* Part recording: objects named in the list get their own part (dl),
   every other object lands together in part 0. NULL name = part 0. */
typedef struct {
	const char *const *names;
	uint8_t count;
	const char *name;
} MeshPartFilter;

static bool mesh_filterPart(void *user, const Object *obj)
{
	const MeshPartFilter *filter = user;

	if (filter->name) return obj->name && strcmp(obj->name, filter->name) == 0;

	for (int i = 0; i < filter->count; i++)
		if (obj->name && strcmp(obj->name, filter->names[i]) == 0) return false;
	return true;
}

static rspq_block_t *mesh_recordPart(Mesh *mesh, MeshPartFilter *filter, const mgfx_matrices_t *palette)
{
	rspq_block_begin();
	model_drawCustom(mesh->model, (ModelDrawConf){
		.userData = filter,
		.filterCb = mesh_filterPart,
		.matrices = palette,
	});
	return rspq_block_end();
}

void mesh_recordObjects(Mesh *mesh)
{
	ModelIter it = model_iterCreate(mesh->model, CHUNK_TYPE_OBJECT);
	while (model_iterNext(&it)) {
		rspq_block_begin();
		model_drawObject(it.object, NULL);
		it.object->userBlock = rspq_block_end();
	}

	mesh->dl         = NULL;
	mesh->dl_count   = 0;
	mesh->dl_buffers = 1;
	mesh->palette    = NULL;
	mesh->visible    = 1;
}

void mesh_recordParts(Mesh *mesh, const char *const *names, uint8_t count)
{
	MeshPartFilter filter = { .names = names, .count = count };
	uint16_t bones = 0;

	/* The draw bakes the palette address into the block, so a skinned mesh
	   needs a block per frame buffer, each over its own run of the palette. */
	mesh->palette    = NULL;
	mesh->dl_buffers = 1;
	if (mesh->skeleton) {
		bones = mesh->skeleton->skeletonRef->boneCount;
		mesh->palette = malloc_uncached(sizeof(mgfx_matrices_t) * FB_COUNT * bones);
		assert(mesh->palette);
		mesh->dl_buffers = FB_COUNT;
	}

	mesh->dl_count = 1 + count;
	mesh->dl = malloc(sizeof(rspq_block_t *) * mesh->dl_count * mesh->dl_buffers);
	assert(mesh->dl);

	for (int part = 0; part < mesh->dl_count; part++) {
		filter.name = part == 0 ? NULL : names[part - 1];

		for (int fb = 0; fb < mesh->dl_buffers; fb++) {
			const mgfx_matrices_t *palette = mesh->palette ? mesh->palette + fb * bones : NULL;
			mesh->dl[part * mesh->dl_buffers + fb] = mesh_recordPart(mesh, &filter, palette);
		}
	}

	mesh->visible = 1;
}

void mesh_updatePalette(Mesh *mesh, const Viewport *viewport, uint8_t fb_index)
{
	if (mesh->palette == NULL) return;

	const Armature *armature = mesh->skeleton;
	uint16_t bones = armature->skeletonRef->boneCount;

	/* The frame's part of the product is shared by every bone: view and
	   view-projection over the model matrix, once. Then one product per bone
	   for each of the two matrices the uniform carries. The bone matrices
	   are model space already (armature_update composed the parent chain). */
	const Matrix4 *model = &mesh->matrix_buffer[fb_index];
	Matrix4 view_model, view_projection_model;
	matrix4_product(&view_model, &viewport->view, model);
	matrix4_product(&view_projection_model, &viewport->view_projection, model);

	mgfx_matrices_t *entry = mesh->palette + fb_index * bones;

	for (uint16_t b = 0; b < bones; b++) {
		Matrix4 mv, mvp;
		matrix4_product(&mv,  &view_model,            &armature->bones[b].matrix);
		matrix4_product(&mvp, &view_projection_model, &armature->bones[b].matrix);

		mgfx_get_matrices(&entry[b], &(mgfx_matrices_parms_t){
			.model_view_projection = mvp.m[0],
			.model_view            = mv.m[0],
			.normal                = mv.m[0],
		});
	}
}
