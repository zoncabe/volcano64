/*
 * Animation instance: streams quantized keyframes from the model's animation
 * data file and writes the interpolated values into its attached targets
 * (usually an armature's bones).
 *
 * Port of tiny3d's t3danim (Copyright (c) 2024 Max Bebök, MIT license, see
 * LICENSE), modified from the original: renamed and rewired to the engine's
 * math types.
 */
#ifndef VOLCANO_64_ANIMATION_H
#define VOLCANO_64_ANIMATION_H

#include <stdio.h>

#include "animation/v64_model.h"
#include "animation/v64_armature.h"

typedef struct {
	float timeStart;
	float timeEnd;
	int32_t* changedFlag; /* flag to increment when target is changed */
} AnimationTargetBase;

typedef struct {
	AnimationTargetBase base;
	Quaternion* targetQuat; /* target to modify */
	Quaternion kfCurr; /* current keyframe value */
	Quaternion kfNext; /* next keyframe value */
} AnimationTargetQuat;

typedef struct {
	AnimationTargetBase base;
	float* targetScalar;
	float kfCurr;
	float kfNext;
} AnimationTargetScalar;

typedef struct {
	AnimationData *animRef;
	AnimationTargetQuat *targetsQuat;
	AnimationTargetScalar *targetsScalar;

	float speed;
	float time;

	FILE *file;
	int nextKfSize;
	uint8_t isPlaying;
	uint8_t isLooping;
} Animation;

/* Creates an animation instance from a model's animation data, by name. */
Animation animation_create(const Model *model, const char* name);

/* Attaches an animation to an armature: every channel targets its bone. */
void animation_attach(Animation* anim, const Armature* armature);

/*
 * Attach a single position/rotation/scale target to a single channel,
 * overriding an earlier 'animation_attach'. 'updateFlag' is set to 1 when
 * the target changed, 2 when the animation rolled over.
 */
void animation_attachPos(Animation* anim, uint32_t targetIdx, Vector3* target, int32_t *updateFlag);
void animation_attachRot(Animation* anim, uint32_t targetIdx, Quaternion* target, int32_t *updateFlag);
void animation_attachScale(Animation* anim, uint32_t targetIdx, Vector3* target, int32_t *updateFlag);

/* Advances the animation and applies the changes to its targets. */
void animation_update(Animation* anim, float deltaTime);

/*
 * Sets the animation to a specific time.
 * Rewinding may cause some work internally due to potential DMAs.
 */
void animation_setTime(Animation* anim, float time);

/* Current time of the animation in seconds. */
static inline float animation_getTime(const Animation* anim)
{
	return anim->time;
}

/* Length of the animation in seconds. */
static inline float animation_getLength(const Animation* anim)
{
	return anim->animRef->duration;
}

/*
 * Sets the playback speed as a factor, default 1.0.
 * Reverse playback (speed < 0) is not supported.
 */
static inline void animation_setSpeed(Animation* anim, float speed)
{
	anim->speed = speed < 0.0f ? 0.0f : speed;
}

/* Play or pause. Works independently of setting the speed to 0. */
static inline void animation_setPlaying(Animation* anim, bool isPlaying)
{
	anim->isPlaying = isPlaying;
}

/*
 * Whether the animation is still playing. Looping animations always are
 * once started; non-looping ones stop at the end.
 */
static inline bool animation_isPlaying(const Animation* anim)
{
	return anim->isPlaying != 0;
}

/*
 * Loop or stop at the end. A non-looping animation needs
 * 'animation_setPlaying' to play again.
 */
static inline void animation_setLooping(Animation* anim, bool loop)
{
	anim->isLooping = loop;
}

/* Frees data allocated in the animation. */
void animation_destroy(Animation *anim);


#endif
