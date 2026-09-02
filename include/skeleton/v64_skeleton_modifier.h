/*
	Post-animation skeleton edits. The animation writes the base pose; the
	modifier list runs in order on top of it (weapons, physics driven bones,
	aim, IK), each writing local bone TRS + hasChanged. The contract: a
	modifier never touches bone matrices and never calls armature_update —
	the skeleton's owner does that once, after the list.
*/
#ifndef VOLCANO_64_SKELETON_MODIFIER_H
#define VOLCANO_64_SKELETON_MODIFIER_H

#include <stdint.h>
#include "animation/v64_armature.h"

#define SKELETON_MODIFIER_MAX 8

typedef void (*SkeletonModifierFn)(Armature *skeleton, void *context);

typedef struct {

	SkeletonModifierFn apply;
	void              *context;

} SkeletonModifier;

typedef struct {

	SkeletonModifier modifier[SKELETON_MODIFIER_MAX];
	uint8_t count;

} SkeletonModifiers;


void skeletonModifiers_add(SkeletonModifiers *modifiers, SkeletonModifierFn apply, void *context);
void skeletonModifiers_apply(SkeletonModifiers *modifiers, Armature *skeleton);

/* Model-space pose of a bone, composed from the local TRS chain: current
   frame, unlike bone->matrix which lags one skeleton update behind. */
void skeleton_getBonePose(const Armature *skeleton, int16_t bone, Vector3 *position, Quaternion *rotation);

#endif
