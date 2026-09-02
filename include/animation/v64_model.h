/*
 * Model file parser: chunks, packed vertices, materials, skeleton and
 * animation data as stored in the model binary.
 *
 * Port of tiny3d's t3dmodel and the CPU side of t3d.h (Copyright (c) 2024
 * Max Bebök, MIT license, see LICENSE), modified from the original: renamed,
 * rewired to the engine's math types, and stripped of the tiny3d ucode API.
 */
#ifndef VOLCANO_64_MODEL_H
#define VOLCANO_64_MODEL_H

#include <libdragon.h>
#include <mgfx.h>

#include "physics/math/v64_vector3.h"
#include "physics/math/v64_quaternion.h"
#include "physics/math/v64_frustum.h"

#define ALPHA_MODE_DEFAULT 0
#define ALPHA_MODE_OPAQUE  1
#define ALPHA_MODE_CUTOUT  2
#define ALPHA_MODE_TRANSP  3

#define FOG_MODE_DEFAULT  0
#define FOG_MODE_DISABLED 1
#define FOG_MODE_ACTIVE   2

#define ANIM_TARGET_TRANSLATION 0
#define ANIM_TARGET_SCALE_XYZ   1
#define ANIM_TARGET_SCALE_S     2
#define ANIM_TARGET_ROTATION    3

/* keyframe times in the animation data files are stored in 1/60s ticks */
#define ANIM_KEYFRAME_TICK (1.0f / 60.0f)


/* Model vertex, straight from the file and fed to the magma ucode as-is.
 * Positions are s16 integers in render units, normals 5,6,5 packed,
 * texcoords 10.5 fixed point in texel coordinates (as tiny3d baked them; the
 * texturing uniform stays at identity). The vertex pipeline is created over
 * this layout. */
typedef struct {
	/* 0x00 */ int16_t position[3];
	/* 0x06 */ uint16_t normal;
	/* 0x08 */ uint32_t rgba;
	/* 0x0C */ int16_t st[2];
} __attribute__((aligned(8))) RenderVertex;

static_assert(sizeof(RenderVertex) == 0x10, "RenderVertex has wrong size");

/* Render flags exported per material (tiny3d's T3DDrawFlags). Kept as parsed
 * from the file; how they map onto magma pipeline state is the renderer's
 * call. */
enum DrawFlags {
	DRAW_FLAG_DEPTH      = 1 << 0,
	DRAW_FLAG_TEXTURED   = 1 << 1,
	DRAW_FLAG_SHADED     = 1 << 2,
	DRAW_FLAG_CULL_FRONT = 1 << 3,
	DRAW_FLAG_CULL_BACK  = 1 << 4,
	DRAW_FLAG_NO_LIGHT   = 1 << 16,
};

/* Vertex effect functions a material may request (tiny3d's T3DVertexFX). */
enum VertexFX {
	VERTEX_FX_NONE           = 0,
	VERTEX_FX_SPHERICAL_UV   = 1,
	VERTEX_FX_CELSHADE_COLOR = 2,
	VERTEX_FX_CELSHADE_ALPHA = 3,
	VERTEX_FX_OUTLINE        = 4,
	VERTEX_FX_UV_OFFSET      = 5,
};

typedef struct {
	float low;
	float height;
	int8_t mask;
	int8_t shift;
	uint8_t mirror;
	uint8_t clamp;
} MaterialAxis;

typedef struct {
	uint32_t texReference; /* dynamic/offscreen texture if non-zero, can be set in fast64 */
	char* texPath;
	uint32_t textureHash;
	sprite_t* texture;
	uint16_t texWidth;
	uint16_t texHeight;

	MaterialAxis s;
	MaterialAxis t;
} MaterialTexture;

typedef struct {
	uint64_t colorCombiner;
	uint64_t otherModeValue;
	uint64_t otherModeMask;
	uint32_t blendMode;
	uint32_t renderFlags;

	uint8_t _unused00_; /* see: ALPHA_MODE_xxx */
	uint8_t fogMode; /* see: FOG_MODE_xxx */
	uint8_t setColorFlags;
	uint8_t vertexFxFunc;

	color_t primColor;
	color_t envColor;
	color_t blendColor;

	char* name;
	MaterialTexture textureA;
	MaterialTexture textureB;
} Material;

typedef struct {
	char* name;
	uint32_t vertexCount;
	uint32_t indexCount;
	Material* material;
	/* can be used freely by the user for recording, freed with the model */
	rspq_block_t *userBlock;
	uint8_t isVisible; /* set by culling checks, otherwise no effect on rendering */
	uint8_t _padding;
	uint8_t userValue0; /* free values usable by users */
	uint8_t userValue1; /* free values usable by users */
	int16_t aabbMin[3];
	int16_t aabbMax[3];

	/* Slices of the model's vertex/index/bone-index chunks. Stored as byte
	   offsets in the file, patched to pointers at load time. Indices are
	   object-local: they address 'vertices', which is what the draw binds. */
	RenderVertex *vertices;
	uint16_t *indices;
	uint8_t *boneIndices; /* NULL when the object is not skinned */
} Object;

typedef struct {
	int16_t aabbMin[3];
	int16_t aabbMax[3];
	uint16_t value;
} BvhNode;

typedef struct {
	uint16_t nodeCount;
	uint16_t dataCount;
	BvhNode nodes[];
	/* uint16_t data[]; // Object pointer, shifted by 3, relative to 'objectBasePtr' */
} Bvh;

typedef struct {
	char* name;
	uint16_t parentIdx;
	uint16_t depth;
	Vector3 scale;
	Quaternion rotation;
	Vector3 position;
} BoneData;

typedef struct {
	uint16_t boneCount;
	uint16_t _reserved;
	BoneData bones[];
} SkeletonData;

typedef struct {
	uint16_t targetIdx;
	uint8_t targetType;
	uint8_t attributeIdx;
	float quantScale;
	float quantOffset;
} AnimationChannelMapping;

typedef struct {
	char* name;
	float duration;
	uint32_t keyframeCount;
	uint16_t channelsQuat;
	uint16_t channelsScalar;
	char* filePath;
	AnimationChannelMapping channelMappings[];
} AnimationData;

typedef union {
	char type;
	uint32_t offset;
} ChunkOffset;

typedef struct {
	char magic[4];
	uint32_t chunkCount;

	uint16_t totalVertCount;
	uint16_t totalIndexCount;

	uint32_t chunkIdxVertices;
	uint32_t chunkIdxIndices;
	uint32_t chunkIdxMaterials;
	char* stringTablePtr;

	/* can be used freely by the user for recording, freed with the model */
	rspq_block_t *userBlock;

	int16_t aabbMin[3];
	int16_t aabbMax[3];

	ChunkOffset chunkOffsets[];
} Model;

typedef struct {
	union {
		void* chunk;
		Object *object;
		Material *material;
		SkeletonData *skeleton;
		AnimationData *anim;
	};

	const Model *_model;
	uint16_t _idx;
	char _chunkType;
} ModelIter;

/* Types of chunks contained in a Model. */
enum ModelChunkType {
	CHUNK_TYPE_VERTICES     = 'V',
	CHUNK_TYPE_INDICES      = 'I',
	CHUNK_TYPE_MATERIAL     = 'M',
	CHUNK_TYPE_OBJECT       = 'O',
	CHUNK_TYPE_SKELETON     = 'S',
	CHUNK_TYPE_ANIM         = 'A',
	CHUNK_TYPE_BVH          = 'B',
	CHUNK_TYPE_BONE_INDICES = 'J'
};


/* Loads a model from a file. Free it with 'model_free'. */
Model* model_load(const char *path);

/* Frees the model and any related resources (e.g. textures). */
void model_free(Model* model);

/* Returns the global vertex buffer of a model, shared by all objects. */
static inline RenderVertex* model_getVertices(const Model *model)
{
	uint32_t offset = model->chunkOffsets[model->chunkIdxVertices].offset & 0x00FFFFFF;
	return (RenderVertex*)((char*)model + offset);
}

/* Returns the first/main skeleton of a model, NULL if it has none. */
static inline const SkeletonData* model_getSkeleton(const Model *model)
{
	for(uint32_t i = 0; i < model->chunkCount; i++) {
		if(model->chunkOffsets[i].type == CHUNK_TYPE_SKELETON) {
			uint32_t offset = model->chunkOffsets[i].offset & 0x00FFFFFF;
			return (SkeletonData*)((char*)model + offset);
		}
	}
	return NULL;
}

/* Returns the number of animations in the model. */
static inline uint32_t model_getAnimationCount(const Model *model)
{
	uint32_t count = 0;
	for(uint32_t i = 0; i < model->chunkCount; i++) {
		if(model->chunkOffsets[i].type == CHUNK_TYPE_ANIM)count++;
	}
	return count;
}

/* Stores the pointers to all animations inside the model into 'anims'.
 * Use 'model_getAnimationCount' to allocate enough memory. */
void model_getAnimations(const Model *model, AnimationData **anims);

/* Returns an animation definition by name, NULL if not found. */
AnimationData* model_getAnimation(const Model *model, const char* name);

/* Returns an object by name, NULL if not found. */
Object* model_getObject(const Model *model, const char *name);

/* Returns an object by index. No bounds checking is done. */
static inline Object* model_getObjectByIndex(const Model *model, uint32_t index)
{
	uint32_t offset = model->chunkOffsets[index].offset & 0x00FFFFFF;
	return (Object*)((char*)model + offset);
}

/* Returns a material by name, NULL if not found. */
Material* model_getMaterial(const Model *model, const char *name);

/* Creates an iterator to traverse the chunks of a model:
 *
 *   ModelIter it = model_iterCreate(model, CHUNK_TYPE_OBJECT);
 *   while(model_iterNext(&it)) { ... it.object ... }
 *
 * The iterator does not need to be freed; the model must outlive it. */
static inline ModelIter model_iterCreate(const Model *model, enum ModelChunkType chunkType)
{
	return (ModelIter){
		.chunk = NULL,
		._model = model,
		._idx = 0,
		._chunkType = chunkType,
	};
}

/* Advances the iterator to the next chunk of its type. Returns false and
 * sets 'iter->chunk' to NULL once the end is reached. */
bool model_iterNext(ModelIter *iter);

/* Returns the BVH of a model, NULL if it has none (pass '--bvh' to the gltf
 * importer to create one). */
static inline const Bvh* model_bvhGet(const Model *model)
{
	for(uint32_t i = 0; i < model->chunkCount; i++) {
		if(model->chunkOffsets[i].type == CHUNK_TYPE_BVH) {
			uint32_t offset = model->chunkOffsets[i].offset & 0x00FFFFFF;
			return (Bvh*)((char*)model + offset);
		}
	}
	return NULL;
}

/* Queries the BVH of a model with a frustum (in model space), marking every
 * reached object as visible via 'isVisible'. Clear the flags first. */
void model_bvhQueryFrustum(const Bvh *bvh, const Frustum *frustum);


/* --- drawing ---------------------------------------------------------------
 * The draw path runs on the magma ucode: materials apply their RDP state via
 * rdpq, objects turn into vertex buffer binds and indexed draws. */

/* callback for custom drawing, this hooks into the tile-setting section */
typedef void (*ModelTileCb)(void* userData, rdpq_texparms_t *tileParams, rdpq_tile_t tile);
typedef bool (*ModelFilterCb)(void* userData, const Object *obj);
typedef void (*ModelDynTextureCb)(
	void* userData, const Material *material, rdpq_texparms_t *tileParams, rdpq_tile_t tile
);

/* Defines settings and callbacks for custom drawing */
typedef struct {
	void* userData;
	ModelTileCb tileCb; /* callback to modify tile settings */
	ModelFilterCb filterCb; /* callback to filter parts */
	ModelDynTextureCb dynTextureCb; /* callback to set dynamic textures, aka "Texture Reference" in fast64 */
	const mgfx_matrix_t *matrices; /* bone matrix palette for skinned draws */
} ModelDrawConf;

/*
 * State for model and material settings during a draw.
 * This is used to minimize state changes across materials.
 */
typedef struct {
	uint32_t lastTextureHashA;
	uint32_t lastTextureHashB;
	uint8_t lastFogMode;
	uint32_t lastRenderFlags;
	uint64_t lastCC;
	color_t lastPrimColor;
	color_t lastEnvColor;
	color_t lastBlendColor;
	uint8_t lastVertFXFunc;
	uint16_t lastUvGenParams[2];
	uint64_t lastOtherMode;
	uint32_t lastBlendMode;
	ModelDrawConf* drawConf;
} ModelState;

/* Creates a new draw state with default values. */
static inline ModelState model_stateCreate(void)
{
	return (ModelState){
		.lastFogMode = 0xFF,
		.lastVertFXFunc = VERTEX_FX_NONE,
		.lastOtherMode = 0xFF,
		.lastBlendMode = 0xFFFFFFFF
	};
}

/* Hands the module the pipeline's texturing uniform, once at render init:
 * materials load their texture size through it so the shader turns the
 * normalized texcoords into texel coordinates. */
void model_setTexturingUniform(const mg_uniform_t *uniform);

/* Draws a model with a custom configuration. */
void model_drawCustom(const Model* model, ModelDrawConf conf);

/* Draws a model with default settings. */
static inline void model_draw(const Model* model)
{
	model_drawCustom(model, (ModelDrawConf){
		.userData = NULL,
		.tileCb = NULL,
		.filterCb = NULL
	});
}

/*
 * Draws an object in a model directly: only the mesh part, no material or
 * texture settings. Pass the bone matrix palette for skinned meshes, NULL
 * for non-skinned. Use 'model_drawMaterial' before this to change materials.
 */
void model_drawObject(const Object *object, const mgfx_matrix_t *boneMatrices);

/*
 * Draws/applies a material of an object, before 'model_drawObject'.
 * Pass NULL for 'state' when recording individual objects into a display
 * list; for complete recordings create one via 'model_stateCreate' and pass
 * it to each call.
 */
void model_drawMaterial(Material *mat, ModelState *state);


/* Vertex-buffer helpers. */

static inline int16_t* vertbuffer_getPos(RenderVertex vert[], int idx)
{
	return vert[idx].position;
}

static inline int16_t* vertbuffer_getUv(RenderVertex vert[], int idx)
{
	return vert[idx].st;
}

static inline uint32_t* vertbuffer_getColor(RenderVertex vert[], int idx)
{
	return &vert[idx].rgba;
}

static inline uint8_t* vertbuffer_getRgba(RenderVertex vert[], int idx)
{
	return (uint8_t*)&vert[idx].rgba;
}

static inline uint16_t* vertbuffer_getNorm(RenderVertex vert[], int idx)
{
	return &vert[idx].normal;
}


#endif
