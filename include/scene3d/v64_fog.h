#ifndef VOLCANO_64_FOG_H
#define VOLCANO_64_FOG_H

#include <stdbool.h>
#include <libdragon.h>
#include <magma.h>

/* Distance fog: computed per vertex on the RSP and blended by the RDP.
   Range is in world units along the view axis, inside the camera planes. */

typedef struct {

	color_t color;
	float near;
	float far;
	bool enabled;

} FogDef;

typedef struct {

	color_t color;
	float near;
	float far;
	bool enabled;

} Fog;


Fog* fog_get(void);

void fog_init(const FogDef* def);

/* Loads the fog uniform and sets the RDP blender accordingly. Disabled fog
   is start == end for the ucode, plus the blender fog mode off. */
void fog_set(const Fog* fog, const mg_uniform_t *uniform);

#endif
