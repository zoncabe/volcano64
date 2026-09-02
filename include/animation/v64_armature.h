/*
 * Armature: live skeleton instance built from a model's skeleton data.
 * Bone poses are blended and composed on the CPU; the resulting fixed-point
 * matrices are the palette the magma ucode skins vertices with.
 *
 * Port of tiny3d's t3dskeleton (Copyright (c) 2024 Max Bebök, MIT license,
 * see LICENSE), modified from the original: renamed, rewired to the engine's
 * math types, and producing magma's matrix layout. The tiny3d segment-table
 * mechanism for buffered skeletons is gone; the current buffer is resolved
 * on the CPU at draw time.
 */
#ifndef VOLCANO_64_ARMATURE_H
#define VOLCANO_64_ARMATURE_H

#include "animation/v64_model.h"
#include "physics/math/v64_matrix4.h"

/*
 * Bone instance, part of an armature. 'matrix' gets updated by
 * 'armature_update' if 'hasChanged' is set.
 */
typedef struct {
	Matrix4 matrix;
	Vector3 scale;
	Quaternion rotation;
	Vector3 position;
	int32_t hasChanged;
} Bone;

/*
 * Armature instance, constructed from a model's skeleton data.
 * This is what skinned models are drawn with.
 */
typedef struct {
	Bone* bones;
	mgfx_matrix_t* boneMatricesFP; /* fixed point matrices, used for rendering */
	uint8_t bufferCount; /* number of matrix buffers */
	uint8_t currentBufferIdx;
	const SkeletonData* skeletonRef; /* reference to the model, defines the skeleton structure */
} Armature;


/*
 * Creates an armature instance from a model's skeleton data.
 * It reserves multiple matrix buffers so a skeleton can be updated while the
 * last frame is still being rendered. Only the fixed-point matrices are
 * buffered, the bone data itself is not.
 * 'bufferCount' should match the frame-buffer count.
 */
Armature armature_createBuffered(const Model *model, int bufferCount);

static inline Armature armature_create(const Model *model)
{
	return armature_createBuffered(model, 1);
}

/*
 * Returns the bone matrix palette for the next draw call: the buffer that
 * 'armature_update' last wrote.
 */
static inline const mgfx_matrix_t* armature_getMatrices(const Armature *armature)
{
	return &armature->boneMatricesFP[armature->currentBufferIdx * armature->skeletonRef->boneCount];
}

/*
 * Clones an armature. With 'useMatrices' false no matrices are allocated,
 * which is useful for blending animations.
 */
Armature armature_clone(const Armature *armature, bool useMatrices);

/*
 * Resets an armature to its initial state (resting pose).
 * To recalculate the bone matrices too, call 'armature_update' afterwards.
 */
void armature_reset(Armature *armature);

/*
 * Blends two armatures together. It is safe to use the same armature as an
 * input and output parameter. The factor may go beyond [0,1] to "overdrive"
 * animations.
 */
void armature_blend(const Armature *res, const Armature *a, const Armature *b, float factor);

/*
 * Updates the armature's bone matrices if bone data has changed.
 * Call this after making changes to a bone's pos/rot/scale; the bone's
 * 'hasChanged' flag must also be set.
 */
void armature_update(Armature *armature);

/* Frees data allocated in the armature. Safe to call multiple times. */
void armature_destroy(Armature *armature);

/* Returns the index of the bone with the given name, or -1 if not found. */
int armature_findBone(Armature *armature, const char *name);

/*
 * Gets the position in model space of a bone from its matrix.
 * Assumes the bone matrix was updated beforehand with 'armature_update'.
 */
static inline Vector3 armature_getBonePosModelSpace(const Armature *armature, int boneIdx)
{
	assertf(boneIdx >= 0 && boneIdx < armature->skeletonRef->boneCount,
		"Bone index is out of bounds, idx: %i, boneCount: %i", boneIdx, armature->skeletonRef->boneCount);
	const Bone *bone = &armature->bones[boneIdx];

	return (Vector3){
		bone->matrix.m[3][0],
		bone->matrix.m[3][1],
		bone->matrix.m[3][2]
	};
}


#endif
