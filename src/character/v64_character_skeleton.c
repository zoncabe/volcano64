#include <assert.h>
#include <fgeom.h>
#include <fmath.h>

#include "character/v64_character_skeleton.h"


void skeletonModifiers_add(SkeletonModifiers *modifiers, SkeletonModifierFn apply, void *context)
{
	assert(modifiers->count < SKELETON_MODIFIER_MAX);
	modifiers->modifier[modifiers->count++] = (SkeletonModifier){ apply, context };
}


void skeletonModifiers_apply(SkeletonModifiers *modifiers, Armature *skeleton)
{
	for (uint8_t i = 0; i < modifiers->count; i++)
		modifiers->modifier[i].apply(skeleton, modifiers->modifier[i].context);
}


void skeleton_getBonePose(const Armature *skeleton, int16_t bone, Vector3 *position, Quaternion *rotation)
{
	uint16_t chain[16];
	int depth = 0;

	uint16_t idx = (uint16_t)bone;
	while (idx != 0xFFFF && depth < 16) {
		chain[depth++] = idx;
		idx = skeleton->skeletonRef->bones[idx].parentIdx;
	}

	*position = (Vector3){ 0.0f, 0.0f, 0.0f };
	*rotation = (Quaternion){ 0.0f, 0.0f, 0.0f, 1.0f };

	for (int i = depth - 1; i >= 0; i--) {
		const Bone *b = &skeleton->bones[chain[i]];

		Vector3 step = quaternion_rotateVector(rotation, &b->position);
		position->x += step.x;
		position->y += step.y;
		position->z += step.z;

		*rotation = quaternion_product(rotation, &b->rotation);
	}
}

