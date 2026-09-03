/*
 * Model file parser, material application and drawing.
 *
 * Port of tiny3d's t3dmodel (Copyright (c) 2024 Max Bebök, MIT license, see
 * LICENSE), modified from the original: renamed, rewired to the engine's
 * math, reading the V64 model format (flat vertices and indices, from this
 * repo's gltf_to_model importer) and drawing through the magma ucode.
 */
#include <magma.h>

#include "animation/v64_model.h"

#define MODEL_VERSION 0x01

static inline void* patch_pointer(void *ptr, uint32_t offset) {
  return (void*)(offset + (int32_t)ptr);
}

static inline bool is_power_of_two(uint16_t x) {
  return (x & (x - 1)) == 0;
}

typedef struct {
  uint16_t objectPtr;
} BvhData;

typedef struct {
  uint32_t hash;
  sprite_t *texture;
  uint32_t count;
} TextureEntry;

static uint32_t textureCacheSize = 0;
static TextureEntry *textureCache = NULL;

static sprite_t* texture_cache_get(uint32_t hash) {
  for(uint32_t i = 0; i < textureCacheSize; i++) {
    if(textureCache[i].hash == hash) {
      textureCache[i].count++;
      return textureCache[i].texture;
    }
  }
  return NULL;
}

static void texture_cache_add(uint32_t hash, sprite_t *texture) {
  TextureEntry *cacheEntry = NULL;
  for(uint32_t i = 0; i < textureCacheSize; i++) {
    if(textureCache[i].hash == 0) {
      cacheEntry = &textureCache[i];
      break;
    }
  }

  if(cacheEntry == NULL) {
    textureCacheSize++;
    if(textureCache == NULL) {
      textureCache = malloc(sizeof(TextureEntry));
    } else {
      textureCache = realloc(textureCache, sizeof(TextureEntry) * textureCacheSize);
    }
    cacheEntry = &textureCache[textureCacheSize-1];
  }

  cacheEntry->hash = hash;
  cacheEntry->texture = texture;
  cacheEntry->count = 1;
}

static void texture_cache_free(uint32_t hash)
{
  for(uint32_t i = 0; i < textureCacheSize; i++) {
    if(textureCache[i].hash == hash) {
      textureCache[i].count--;
      if(textureCache[i].count == 0) {
        sprite_free(textureCache[i].texture);
        textureCache[i].hash = 0;
      }
      return;
    }
  }
}

static void texture_cache_free_mem()
{
  uint32_t emptyEntries = 0;

  for(uint32_t i = 0; i < textureCacheSize; i++) {
    if(textureCache[i].hash == 0) {
      emptyEntries++;
    }
  }

  if(textureCache && emptyEntries == textureCacheSize)
  {
    free(textureCache);
    textureCache = NULL;
    textureCacheSize = 0;
  }
}

Model *model_load(const char *path) {
  int size = 0;
  Model* model = asset_load(path, &size);
  int32_t ptrOffset = (int32_t)(void*)model;

  if(memcmp(model->magic, "V64", 3) != 0) {
    assertf(false, "Invalid model file: %s", path);
  }
  assertf(model->magic[3] == MODEL_VERSION,
    "Invalid model version: %d != %d\n"
    "Please re-export the model with a matching importer",
    MODEL_VERSION, model->magic[3]);

  void* basePtrVertices = (char*)model + (model->chunkOffsets[model->chunkIdxVertices].offset & 0xFFFFFF);
  void* basePtrIndices = (char*)model + (model->chunkOffsets[model->chunkIdxIndices].offset & 0xFFFFFF);
  model->stringTablePtr = patch_pointer(model->stringTablePtr, ptrOffset);

  /* per-vertex bone indices live in their own chunk, present only for
   * skinned models */
  void* basePtrBoneIndices = NULL;
  for(uint32_t i = 0; i < model->chunkCount; i++) {
    if(model->chunkOffsets[i].type == CHUNK_TYPE_BONE_INDICES) {
      basePtrBoneIndices = (char*)model + (model->chunkOffsets[i].offset & 0x00FFFFFF);
      break;
    }
  }

  for(uint32_t i = 0; i < model->chunkCount; i++)
  {
    char chunkType = model->chunkOffsets[i].type;
    uint32_t offset = model->chunkOffsets[i].offset & 0x00FFFFFF;

    if(chunkType == CHUNK_TYPE_OBJECT) {
      Object *obj = (Object*)((char*)model + offset);
      if(obj->name != NULL) {
        obj->name = patch_pointer(obj->name, (uint32_t)model->stringTablePtr);
      }

      uint32_t matIdx = model->chunkIdxMaterials + (uint32_t)obj->material;
      obj->material = (Material*)((char*)model + (model->chunkOffsets[matIdx].offset & 0xFFFFFF));

      obj->vertices = patch_pointer(obj->vertices, (uint32_t)basePtrVertices);
      obj->indices = patch_pointer(obj->indices, (uint32_t)basePtrIndices);

      if((uint32_t)obj->boneIndices == 0xFFFFFFFF) {
        obj->boneIndices = NULL;
      } else {
        assert(basePtrBoneIndices);
        obj->boneIndices = patch_pointer(obj->boneIndices, (uint32_t)basePtrBoneIndices);
      }
    }

    if(chunkType == CHUNK_TYPE_MATERIAL) {
      Material *mat = (Material*)((char*)model + offset);

      if(mat->name)mat->name += (uint32_t)model->stringTablePtr;
      if(mat->textureA.texPath)mat->textureA.texPath += (uint32_t)model->stringTablePtr;
      if(mat->textureB.texPath)mat->textureB.texPath += (uint32_t)model->stringTablePtr;
    }

    if(chunkType == CHUNK_TYPE_SKELETON) {
      SkeletonData *skel = (SkeletonData*)((char*)model + offset);
      for(int j = 0; j < skel->boneCount; j++) {
        BoneData *bone = &skel->bones[j];
        bone->name = patch_pointer(bone->name, (uint32_t)model->stringTablePtr);
      }
    }

    if(chunkType == CHUNK_TYPE_ANIM) {
      AnimationData *anim = (AnimationData*)((char*)model + offset);
      anim->name = patch_pointer(anim->name, (uint32_t)model->stringTablePtr);
      anim->filePath = patch_pointer(anim->filePath, (uint32_t)model->stringTablePtr);
    }

    if(chunkType == CHUNK_TYPE_BVH) {
      /* node leafs are stored as indices to the objects, we convert that to
       * a relative address to the actual object, shifted by 2 since it's
       * 4 byte aligned (and nodes use 16bit indices) */
      Bvh *bvh = (Bvh*)((char*)model + offset);
      BvhData *data = (BvhData*)&bvh->nodes[bvh->nodeCount]; /* data is right after nodes */

      for(int d=0; d<bvh->dataCount; ++d) {
        Object *obj = model_getObjectByIndex(model, data[d].objectPtr);
        uint32_t addr = (uint32_t)bvh - (uint32_t)obj;
        assert((addr & 0b11) == 0);
        addr >>= 2;
        assert(addr < 0x10000);
        data[d].objectPtr = addr;
      }
    }
  }

  data_cache_hit_writeback_invalidate(model, size);
  return model;
}

void model_free(Model *model) {
  bool txtErased = false;

  if(model->userBlock) {
    rspq_block_free(model->userBlock);
  }

  for(uint32_t c = 0; c < model->chunkCount; c++)
  {
    char chunkType = model->chunkOffsets[c].type;
    if(chunkType == CHUNK_TYPE_MATERIAL) {
      Material *mat = (Material*)((char*)model + (model->chunkOffsets[c].offset & 0x00FFFFFF));
      if(mat->textureA.texture) {
        texture_cache_free(mat->textureA.textureHash);
        txtErased = true;
      }
      if(mat->textureB.texture) {
        texture_cache_free(mat->textureB.textureHash);
        txtErased = true;
      }
    }
    if(chunkType == CHUNK_TYPE_OBJECT) {
      Object *obj = (Object*)((char*)model + (model->chunkOffsets[c].offset & 0x00FFFFFF));
      if(obj->userBlock)rspq_block_free(obj->userBlock);
    }
  }
  free(model);
  if(txtErased) texture_cache_free_mem();
}

AnimationData *model_getAnimation(const Model *model, const char *name) {
  for(uint32_t i = 0; i < model->chunkCount; i++) {
    if(model->chunkOffsets[i].type == CHUNK_TYPE_ANIM) {
      uint32_t offset = model->chunkOffsets[i].offset & 0x00FFFFFF;
      AnimationData *anim = (AnimationData*)((char*)model + offset);
      if(strcmp(anim->name, name) == 0)return anim;
    }
  }
  return NULL;
}

Object* model_getObject(const Model *model, const char *name) {
  for(uint32_t i = 0; i < model->chunkCount; i++) {
    if(model->chunkOffsets[i].type == CHUNK_TYPE_OBJECT) {
      uint32_t offset = model->chunkOffsets[i].offset & 0x00FFFFFF;
      Object *obj = (Object*)((char*)model + offset);
      if(obj->name && strcmp(obj->name, name) == 0)return obj;
    }
  }
  return NULL;
}

void model_getAnimations(const Model *model, AnimationData **anims) {
  uint32_t count = 0;
  for(uint32_t i = 0; i < model->chunkCount; i++) {
    if(model->chunkOffsets[i].type == CHUNK_TYPE_ANIM) {
      uint32_t offset = model->chunkOffsets[i].offset & 0x00FFFFFF;
      anims[count++] = (AnimationData*)((char*)model + offset);
    }
  }
}

Material *model_getMaterial(const Model *model, const char *name) {
  for(uint32_t i = 0; i < model->chunkCount; i++) {
    if(model->chunkOffsets[i].type == CHUNK_TYPE_MATERIAL) {
      uint32_t offset = model->chunkOffsets[i].offset & 0x00FFFFFF;
      Material *mat = (Material*)((char*)model + offset);
      if(mat->name && strcmp(mat->name, name) == 0)return mat;
    }
  }
  return NULL;
}

bool model_iterNext(ModelIter *iter) {
  for(; iter->_idx < iter->_model->chunkCount; iter->_idx++) {
    if(iter->_model->chunkOffsets[iter->_idx].type == iter->_chunkType) {
      uint32_t offset = iter->_model->chunkOffsets[iter->_idx].offset & 0x00FFFFFF;
      iter->chunk = (char*)iter->_model + offset;
      iter->_idx++;
      return true;
    }
  }
  iter->chunk = NULL;
  return false;
}

/* context for the functions below, this avoids blowing up the stack-size */
static const Frustum *ctxFrustum;
static const BvhData *ctxData;
static uint32_t ctxBasePtr;

static void bvh_query_node(const BvhNode *node) {
  int dataCount = node->value & 0b1111;
  int offset = (int16_t)node->value >> 4;

  if(dataCount == 0) {
    if(frustum_vsAabbS16(ctxFrustum, node->aabbMin, node->aabbMax)) {
      bvh_query_node(&node[offset]);
      bvh_query_node(&node[offset + 1]);
    }
    return;
  }

  int offsetEnd = offset + dataCount;
  while(offset < offsetEnd) {
    Object* obj = (Object*)(ctxBasePtr - (ctxData[offset++].objectPtr << 2));
    if(frustum_vsAabbS16(ctxFrustum, obj->aabbMin, obj->aabbMax)) {
      obj->isVisible = true;
    }
  }
}

void model_bvhQueryFrustum(const Bvh *bvh, const Frustum *frustum) {
  const BvhData *data = (BvhData*)&bvh->nodes[bvh->nodeCount]; /* data starts right after nodes */
  ctxFrustum = frustum;
  ctxData = data;
  ctxBasePtr = (uint32_t)(char*)bvh;
  bvh_query_node(bvh->nodes);
}


/* --- drawing -------------------------------------------------------------- */

static ModelState dummyState;
static const mg_uniform_t *texturing_uniform;
static const mg_uniform_t *matrices_uniform;

void model_setTexturingUniform(const mg_uniform_t *uniform)
{
  texturing_uniform = uniform;
}

void model_setMatricesUniform(const mg_uniform_t *uniform)
{
  matrices_uniform = uniform;
}

static void set_texture(Material *mat, rdpq_tile_t tile, ModelDrawConf *conf)
{
  MaterialTexture *tex = tile == TILE0 ? &mat->textureA : &mat->textureB;
  if(tex->texPath || tex->texReference)
  {
    if(tex->texPath && !tex->texture) {
      tex->texture = texture_cache_get(tex->textureHash);
      if(tex->texture == NULL) {
        tex->texture = sprite_load(tex->texPath);
        texture_cache_add(tex->textureHash, tex->texture);
      }
    }

    rdpq_texparms_t texParam = (rdpq_texparms_t){};
    texParam.s.translate = tex->s.low;
    texParam.s.mirror = tex->s.mirror;
    texParam.s.repeats = REPEAT_INFINITE;
    texParam.s.scale_log = (int)tex->s.shift;

    if(tex->s.clamp) {
      if(is_power_of_two(tex->texWidth)) {
        texParam.s.repeats = (tex->s.height-tex->s.low+1) / (float)tex->texWidth;
      } else {
        texParam.s.repeats = 1;
      }
    }

    texParam.t.translate = tex->t.low;
    texParam.t.mirror = tex->t.mirror;
    texParam.t.repeats = REPEAT_INFINITE;
    texParam.t.scale_log = (int)tex->t.shift;

    if(tex->t.clamp) {
      if(is_power_of_two(tex->texHeight)) {
        texParam.t.repeats = (tex->t.height-tex->t.low+1) / (float)tex->texHeight;
      } else {
        texParam.t.repeats = 1;
      }
    }

    if(conf && conf->tileCb) {
      conf->tileCb(conf->userData, &texParam, tile);
    }

    if(tex->texReference) {
      if(conf && conf->dynTextureCb)conf->dynTextureCb(conf->userData, mat, &texParam, tile);
    } else {
      if(tile == TILE1 && mat->textureA.textureHash == mat->textureB.textureHash) {
        rdpq_tex_reuse(TILE1, &texParam);
      } else {
        rdpq_sprite_upload(tile, tex->texture, &texParam);
      }
    }
  }
}

void model_drawMaterial(Material *mat, ModelState *state)
{
  if(!state) {
    dummyState = model_stateCreate();
    state = &dummyState;
  }

  /* Material fog toggling and vertex effects were tiny3d ucode state; fog is
   * scene-level with magma, and effects like env mapping are a different
   * vertex shader. Both are the renderer's call now, not the material's. */

  /* rdpq settings, these only need to happen before a draw */
  if(mat->colorCombiner)
  {
    bool setBlendMode  = state->lastBlendMode != mat->blendMode;
    bool setCC         = mat->colorCombiner != state->lastCC;
    bool setTexture    = state->lastTextureHashA != mat->textureA.textureHash || state->lastTextureHashB != mat->textureB.textureHash;
    bool setOtherMode  = state->lastOtherMode != mat->otherModeValue || setTexture;
    bool setPrimColor  = (mat->setColorFlags & 0b001) && color_to_packed32(state->lastPrimColor) != color_to_packed32(mat->primColor);
    bool setEnvColor   = (mat->setColorFlags & 0b010) && color_to_packed32(state->lastEnvColor) != color_to_packed32(mat->envColor);
    bool setBlendColor = (mat->setColorFlags & 0b100) || (mat->otherModeValue & SOM_ALPHACOMPARE_THRESHOLD);
    setBlendColor = setBlendColor && color_to_packed32(state->lastBlendColor) != color_to_packed32(mat->blendColor);

    if(setTexture)
    {
      state->lastTextureHashA = mat->textureA.textureHash;
      state->lastTextureHashB = mat->textureB.textureHash;

      rdpq_tex_multi_begin();
        set_texture(mat, TILE0, state->drawConf);
        set_texture(mat, TILE1, state->drawConf);
      rdpq_tex_multi_end();

      /* texcoords come from the file already in RDP 10.5 texel coords with
       * any half-texel adjust baked per material, exactly as tiny3d shipped
       * them: identity scale and zero offset pass them through untouched
       * (the shader's multiply is a plain integer product). Verified on
       * screen: an extra half-texel offset here shifts the textures. */
      if(texturing_uniform) {
        mgfx_set_texturing_inline(texturing_uniform, &(mgfx_texturing_parms_t){
          .scale  = { 1, 1 },
          .offset = { 0, 0 },
        });
      }
    }

    rdpq_mode_begin();

    if(setCC) {
      state->lastCC = mat->colorCombiner;
      rdpq_mode_combiner(mat->colorCombiner);
    }

    if(setBlendMode) {
      rdpq_mode_blender(mat->blendMode);
      state->lastBlendMode = mat->blendMode;
    }

    if(setPrimColor) {
      state->lastPrimColor = mat->primColor;
      rdpq_set_prim_color(mat->primColor);
    }

    if(setBlendColor) {
      state->lastBlendColor = mat->blendColor;
      rdpq_set_blend_color(mat->blendColor);
    }

    if(setEnvColor) {
      state->lastEnvColor = mat->envColor;
      rdpq_set_env_color(mat->envColor);
    }

    if(setOtherMode) {
      __rdpq_mode_change_som(mat->otherModeMask, mat->otherModeValue);
      state->lastOtherMode = mat->otherModeValue;

      /* workaround for some libdragon changes that handles AA vs. no AA (maps to CVG * alpha)
       * since we use the raw othermodes here it causes issues in not updating properly */
      if(mat->otherModeValue & SOM_ALPHACOMPARE_THRESHOLD) {
        rdpq_mode_alphacompare(mat->blendColor.a);
      } else {
        rdpq_mode_alphacompare(0);
      }
    }

    rdpq_mode_end();
  }

  /* the tiny3d draw flags map onto magma's fixed-function state */
  if(mat->renderFlags != state->lastRenderFlags) {
    state->lastRenderFlags = mat->renderFlags;

    mg_cull_mode_t cull = MG_CULL_MODE_NONE;
    if(mat->renderFlags & DRAW_FLAG_CULL_FRONT)cull = MG_CULL_MODE_FRONT;
    if(mat->renderFlags & DRAW_FLAG_CULL_BACK)cull = MG_CULL_MODE_BACK;
    mg_set_culling(&(mg_culling_parms_t){ .cull_mode = cull });

    mg_geometry_flags_t flags = 0;
    if(mat->renderFlags & DRAW_FLAG_DEPTH)flags |= MG_GEOMETRY_FLAGS_Z_ENABLED;
    if(mat->renderFlags & DRAW_FLAG_TEXTURED)flags |= MG_GEOMETRY_FLAGS_TEX_ENABLED;
    if(mat->renderFlags & DRAW_FLAG_SHADED)flags |= MG_GEOMETRY_FLAGS_SHADE_ENABLED;
    mg_set_geometry_flags(flags);
  }
}

void model_drawObject(const Object *object, const mgfx_matrices_t *palette)
{
  mg_input_assembly_parms_t assembly = {
    .primitive_topology = MG_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
  };

  /* Skinning the magma way: a bone index per vertex, and before each run of
   * vertices on the same bone the input assembly loads that bone's entry of
   * the palette into the matrices uniform. The vertices are stored in bone
   * space (tiny3d's importer layout), so the entry is the full mvp/mv of
   * that bone and no inverse bind is involved. The load is not inline: the
   * palette pointer is baked into the recorded block, so the caller keeps
   * one palette per frame buffer and records a block against each. */
  if (object->boneIndices && palette) {
    assert(matrices_uniform);
    assembly.mtx_indices        = object->boneIndices;
    assembly.mtx_indices_stride = sizeof(uint8_t);
    assembly.matrices           = palette;
    assembly.matrices_stride    = sizeof(mgfx_matrices_t);
    assembly.matrix_uniform     = *matrices_uniform;
  }

  mg_bind_vertex_buffer(object->vertices);

  mg_draw_begin();
    mg_draw_indexed(&assembly, object->indices, object->indexCount, 0);
  mg_draw_end();
}

void model_drawCustom(const Model* model, ModelDrawConf conf)
{
  ModelState state = model_stateCreate();
  state.drawConf = &conf;

  ModelIter it = model_iterCreate(model, CHUNK_TYPE_OBJECT);
  while(model_iterNext(&it))
  {
    if(conf.filterCb && !conf.filterCb(conf.userData, it.object)) {
      continue;
    }

    if(it.object->material) {
      model_drawMaterial(it.object->material, &state);
    }
    model_drawObject(it.object, conf.matrices);
  }
}
