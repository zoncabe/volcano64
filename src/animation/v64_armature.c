/*
 * Port of tiny3d's t3dskeleton (Copyright (c) 2024 Max Bebök, MIT license,
 * see LICENSE). See the header for the differences from the original.
 */
#include <malloc.h>
#include <string.h>

#include "animation/v64_armature.h"

Armature armature_createBuffered(const Model *model, int bufferCount) {
  const SkeletonData *skelRef = model_getSkeleton(model);
  assert(skelRef != NULL);

  Armature armature = (Armature){
    .bones = malloc(sizeof(Bone) * skelRef->boneCount),
    .boneMatricesFP = malloc_uncached(sizeof(mgfx_matrix_t) * skelRef->boneCount * bufferCount),
    .skeletonRef = skelRef,
    .bufferCount = bufferCount,
    .currentBufferIdx = 0,
  };

  armature_reset(&armature);

  /* Compose the rest pose right away: the bone matrices are read in float
   * by the palette, and uninitialised memory there traps the FPU. */
  armature_update(&armature);
  return armature;
}

Armature armature_clone(const Armature *armature, bool useMatrices) {
  Armature result = {
    .bones = malloc(sizeof(Bone) * armature->skeletonRef->boneCount),
    .boneMatricesFP = NULL,
    .skeletonRef = armature->skeletonRef,
  };
  memcpy(result.bones, armature->bones, sizeof(Bone) * armature->skeletonRef->boneCount);

  if(useMatrices) {
    size_t copySize = sizeof(mgfx_matrix_t) * armature->skeletonRef->boneCount * armature->bufferCount;
    result.boneMatricesFP = malloc_uncached(copySize);
    memcpy(result.boneMatricesFP, armature->boneMatricesFP, copySize);
  }
  return result;
}

void armature_reset(Armature *armature) {
  for(int i = 0; i < armature->skeletonRef->boneCount; i++) {
    const BoneData *boneDef = &armature->skeletonRef->bones[i];
    armature->bones[i].scale = boneDef->scale;
    armature->bones[i].rotation = boneDef->rotation;
    armature->bones[i].position = boneDef->position;
    armature->bones[i].hasChanged = true;
  }
}

void armature_blend(const Armature *res, const Armature *a, const Armature *b, float factor) {
  for(int i = 0; i < res->skeletonRef->boneCount; i++) {
    Bone *boneRes = &res->bones[i];
    Bone *boneA = &a->bones[i];
    Bone *boneB = &b->bones[i];

    boneRes->hasChanged = true;
    boneRes->rotation = quaternion_nlerp(&boneA->rotation, &boneB->rotation, factor);
    boneRes->position = vector3_lerp(&boneA->position, &boneB->position, factor);
    boneRes->scale = vector3_lerp(&boneA->scale, &boneB->scale, factor);
  }
}

void armature_update(Armature *armature)
{
  int updateLevel = -1;
  bool forceUpdate = false;

  mgfx_matrix_t* matStackFP = NULL;

  for(int i = 0; i < armature->skeletonRef->boneCount; i++)
  {
    Bone *bone = &armature->bones[i];
    const BoneData *boneDef = &armature->skeletonRef->bones[i];

    if(forceUpdate && boneDef->depth <= updateLevel) {
      forceUpdate = false;
      updateLevel = -1;
    }

    if(bone->hasChanged || forceUpdate)
    {
      // only cycle through matrices if at least one bone changes.
      // this avoids flickering at the end of an animation, since it would cycle through the last X frames otherwise.
      if(matStackFP == NULL)
      {
        armature->currentBufferIdx = (armature->currentBufferIdx + 1) % armature->bufferCount;
        matStackFP = &armature->boneMatricesFP[armature->skeletonRef->boneCount * armature->currentBufferIdx];
      }

      // if a bone changed we need to also update any children.
      // To do so, update all following bones until we hit one that has the same depth as the changed bone.
      if(!forceUpdate)updateLevel = boneDef->depth;
      forceUpdate = true;

      if(boneDef->parentIdx != 0xFFFF) {
        Matrix4 tmp;
        matrix4_fromSrt(&tmp, &bone->scale, &bone->rotation, &bone->position);
        matrix4_product(&bone->matrix, &armature->bones[boneDef->parentIdx].matrix, &tmp);
      } else {
        matrix4_fromSrt(&bone->matrix, &bone->scale, &bone->rotation, &bone->position);
      }

      matrix4_toFixed(&matStackFP[i], &bone->matrix);

      // if a bone has changed, we need to force updating it until it reached all buffers.
      // otherwise once the updating stops, and we cycle through buffers still, it would flicker.
      // counting up here is also safe when this flag is set to 1 or 'true' externally (e.g.: in animation_update)
      if(armature->bones[i].hasChanged++ == armature->bufferCount) {
        armature->bones[i].hasChanged = 0;
      }
    }
  }
}

int armature_findBone(Armature *armature, const char *name) {
  for(int i = 0; i < armature->skeletonRef->boneCount; i++) {
    if(strcmp(armature->skeletonRef->bones[i].name, name) == 0) {
      return i;
    }
  }
  return -1;
}

void armature_destroy(Armature *armature) {
  if(armature->bones != NULL) {
    free(armature->bones);
    armature->bones = NULL;
  }
  if(armature->boneMatricesFP != NULL) {
    free_uncached(armature->boneMatricesFP);
    armature->boneMatricesFP = NULL;
  }
  armature->skeletonRef = NULL;
}
