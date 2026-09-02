/*
 * Port of tiny3d's t3danim (Copyright (c) 2024 Max Bebök, MIT license, see
 * LICENSE). See the header for the differences from the original.
 */
#include <malloc.h>

#include "animation/v64_animation.h"
#include "physics/math/v64_math_common.h"

// Maps the input data streamed from the animation data file
typedef struct {
  uint16_t nextTime;
  uint16_t channelIdx;
  uint16_t data[2]; // can be either 1 or 2 16-bit values (scalar / quat)
} AnimationKF;

Animation animation_create(const Model *model, const char *name) {
  AnimationData* animDef = model_getAnimation(model, name);
  assertf(animDef, "Animation '%s' not found in model", name);

  return (Animation){
    .animRef = animDef,
    .targetsScalar = NULL,
    .targetsQuat = NULL,
    .time = 0.0f,
    .speed = 1.0f,
    .nextKfSize = sizeof(AnimationKF),
    .file = asset_fopen(animDef->filePath, NULL),
    .isPlaying = 1,
    .isLooping = 1
  };
}

static void rewind_anim(Animation *anim)
{
  for(int c=0; c<anim->animRef->channelsScalar; c++) {
    anim->targetsScalar[c].base.timeEnd = 0;
  }
  for(int c=0; c<anim->animRef->channelsQuat; c++) {
    anim->targetsQuat[c].base.timeEnd = 0;
  }
  anim->nextKfSize = sizeof(AnimationKF);
  rewind(anim->file);
}

void animation_attach(Animation *anim, const Armature *armature) {
  if(anim->targetsQuat)free(anim->targetsQuat);

  size_t allocQuat = sizeof(AnimationTargetQuat) * anim->animRef->channelsQuat;
  size_t allocScalar = sizeof(AnimationTargetScalar) * anim->animRef->channelsScalar;
  anim->targetsQuat = calloc(allocQuat + allocScalar, 1); // only allocate a single block
  anim->targetsScalar = (AnimationTargetScalar*)((uint8_t*)anim->targetsQuat + allocQuat);
  rewind_anim(anim);

  uint32_t channelCount = anim->animRef->channelsScalar + anim->animRef->channelsQuat;

  uint32_t idxQuat = 0;
  uint32_t idxScalar = 0;
  for(uint32_t i = 0; i < channelCount; i++)
  {
    AnimationChannelMapping *channelMap = &anim->animRef->channelMappings[i];
    Bone *bone = &armature->bones[channelMap->targetIdx];

    switch(channelMap->targetType) {
      case ANIM_TARGET_TRANSLATION:
        anim->targetsScalar[idxScalar].targetScalar = vector3_component(&bone->position, channelMap->attributeIdx);
        anim->targetsScalar[idxScalar++].base.changedFlag = &bone->hasChanged;
        break;
      case ANIM_TARGET_SCALE_XYZ:
        anim->targetsScalar[idxScalar].targetScalar = vector3_component(&bone->scale, channelMap->attributeIdx);
        anim->targetsScalar[idxScalar++].base.changedFlag = &bone->hasChanged;
        break;
      case ANIM_TARGET_ROTATION:
        anim->targetsQuat[idxQuat].targetQuat = &bone->rotation;
        anim->targetsQuat[idxQuat++].base.changedFlag = &bone->hasChanged;
      break;
      default: {assertf(false, "Unknown animation target %d", channelMap->targetType);}
    }
  }
}

inline static void attach_scalar(Animation* anim, uint32_t targetIdx, Vector3* target, int32_t *updateFlag, uint8_t targetType) {
  for(int i = 0; i < anim->animRef->channelsScalar; i++) {
    AnimationChannelMapping *channelMap = &anim->animRef->channelMappings[i+anim->animRef->channelsQuat];
    if(channelMap->targetIdx == targetIdx && channelMap->targetType == targetType) {
      anim->targetsScalar[i].targetScalar = vector3_component(target, channelMap->attributeIdx);
      anim->targetsScalar[i].base.changedFlag = updateFlag;
    }
  }
}

void animation_attachPos(Animation* anim, uint32_t targetIdx, Vector3* target, int32_t *updateFlag) {
  attach_scalar(anim, targetIdx, target, updateFlag, ANIM_TARGET_TRANSLATION);
}

void animation_attachScale(Animation *anim, uint32_t targetIdx, Vector3 *target, int32_t *updateFlag) {
  attach_scalar(anim, targetIdx, target, updateFlag, ANIM_TARGET_SCALE_XYZ);
}

void animation_attachRot(Animation *anim, uint32_t targetIdx, Quaternion *target, int32_t *updateFlag) {
  for(int i = 0; i < anim->animRef->channelsQuat; i++) {
    AnimationChannelMapping *channelMap = &anim->animRef->channelMappings[i];
    if(channelMap->targetIdx == targetIdx && channelMap->targetType == ANIM_TARGET_ROTATION) {
      anim->targetsQuat[i].targetQuat = target;
      anim->targetsQuat[i].base.changedFlag = updateFlag;
    }
  }
}

static inline AnimationTargetBase* get_base_target(Animation *anim, uint64_t channelIdx, bool isRot) {
  return isRot ?
    (AnimationTargetBase*)&anim->targetsQuat[channelIdx] :
    (AnimationTargetBase*)&anim->targetsScalar[channelIdx - anim->animRef->channelsQuat];
}

static inline bool load_keyframe(Animation *anim) {
  AnimationKF kf;
  size_t readBytes = fread(&kf, anim->nextKfSize, 1, anim->file);
  if(readBytes == 0)return false;

  bool isLarge = kf.nextTime & 0x8000;
  anim->nextKfSize = isLarge ? sizeof(AnimationKF) : (sizeof(AnimationKF)-2);
  kf.nextTime &= 0x7FFF;

  AnimationChannelMapping *channelMap = &anim->animRef->channelMappings[kf.channelIdx];

  bool isRot = kf.channelIdx < anim->animRef->channelsQuat;
  AnimationTargetBase *targetBase = get_base_target(anim, kf.channelIdx, isRot);

  targetBase->timeStart = targetBase->timeEnd;
  targetBase->timeEnd += (float)kf.nextTime * ANIM_KEYFRAME_TICK;
  if(kf.nextTime == 0)targetBase->timeStart -= 0.00001f; // avoid zero-div for overlapping keyframes

  if(channelMap->targetType == ANIM_TARGET_ROTATION) {
    AnimationTargetQuat *target = (AnimationTargetQuat*)targetBase;
    target->kfCurr = target->kfNext;
    target->kfNext = quaternion_unpacked(kf.data[0], kf.data[1]);
  } else {
    AnimationTargetScalar *target = (AnimationTargetScalar*)targetBase;
    target->kfCurr = target->kfNext;
    target->kfNext = (float)kf.data[0] * channelMap->quantScale + channelMap->quantOffset;
  }

  return true;
}

void animation_update(Animation *anim, float deltaTime) {
  if(!anim->isPlaying)return;
  int32_t updateFlag = 1;
  anim->time += deltaTime * anim->speed;

  if(anim->time >= anim->animRef->duration) {
    anim->time -= anim->animRef->duration;
    rewind_anim(anim);
    updateFlag = 2;

    if(!anim->isLooping) {
      anim->isPlaying = 0;
      return;
    }
  }

  uint32_t channelCount = anim->animRef->channelsScalar + anim->animRef->channelsQuat;
  for(uint32_t c=0; c<channelCount; c++)
  {
    bool isRot = c < anim->animRef->channelsQuat;
    AnimationTargetBase *target = get_base_target(anim, c, isRot);

    while(anim->time >= target->timeEnd) {
      if(!load_keyframe(anim))break;
    }

    float timeDiff = target->timeEnd - target->timeStart;
    float interp = (anim->time - target->timeStart) / timeDiff;
    *target->changedFlag = updateFlag;

    if(isRot) {
      AnimationTargetQuat *t = (AnimationTargetQuat*)target;
      *t->targetQuat = quaternion_nlerp(&t->kfCurr, &t->kfNext, interp);
    } else {
      AnimationTargetScalar *t = (AnimationTargetScalar*)target;
      *t->targetScalar = lerpf(t->kfCurr, t->kfNext, interp);
    }
  }
}

void animation_destroy(Animation *anim) {
  if(anim->targetsQuat)free(anim->targetsQuat); // 'targetsScalar' is part of this memory-block
  if(anim->file)fclose(anim->file);
  anim->targetsQuat = NULL;
  anim->targetsScalar = NULL;
  anim->file = NULL;
}

void animation_setTime(Animation *anim, float time) {
  if(time > anim->animRef->duration)time = anim->animRef->duration;
  if(time < anim->time)rewind_anim(anim);
  anim->time = time;
}
