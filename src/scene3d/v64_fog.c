#include <libdragon.h>
#include <mgfx.h>

#include "scene3d/v64_fog.h"


static Fog fog;

Fog* fog_get(void) { return &fog; }


void fog_init(const FogDef* def)
{
	fog.color   = def->color;
	fog.near    = def->near;
	fog.far     = def->far;
	fog.enabled = def->enabled;
}

void fog_set(const Fog* fog, const mg_uniform_t *uniform)
{
	if (!fog->enabled) {
		/* start == end disables the fog factor in the ucode. */
		mgfx_set_fog_inline(uniform, &(mgfx_fog_parms_t){});
		rdpq_mode_fog(0);
		return;
	}

	rdpq_mode_fog(RDPQ_FOG_STANDARD);
	rdpq_set_fog_color(fog->color);

	mgfx_set_fog_inline(uniform, &(mgfx_fog_parms_t){
		.start = fog->near,
		.end   = fog->far,
	});
}
