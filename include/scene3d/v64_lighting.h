#ifndef VOLCANO_64_LIGHTING_H
#define VOLCANO_64_LIGHTING_H

#include <libdragon.h>
#include <magma.h>
#include "physics/math/v64_math.h"

/* Eight slots (the mgfx cap) and a light takes one whatever its kind, so the
   split between directional and point is the scene's to make. */
#define LIGHT_COUNT 8


typedef enum {

	/* An empty slot. Zero on purpose: the set walks the table in order and
	   stops at the first one, so a scene pays only for what it declared. */
	LIGHT_NONE,

	LIGHT_DIRECTIONAL,
	LIGHT_POINT,

} LightType;


typedef struct {

	LightType type;
	color_t   color;

	union {
		/* Where the light comes from; normalised by the init. */
		struct { Vector3 direction; } directional;

		/* Where it stands and how far it carries. */
		struct { Vector3 position; float size; } point;
	};

} LightSource;


typedef struct {

	color_t ambient_color;
	LightSource source[LIGHT_COUNT];

} LightDef;

typedef LightDef Light;


Light *light_get(void);

/* Copies the scene's declaration into the live lights. */
void light_init(const LightDef *def);

/* Loads the lighting uniform, stopping at the first empty slot. The mgfx
   ucode wants lights in view space, so the frame's view matrix transforms
   them on the way in. */
void light_set(const Light *light, const Matrix4 *view, const mg_uniform_t *uniform);

#endif
