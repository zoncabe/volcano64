#include <stdint.h>
#include <libdragon.h>
#include <magma.h>
#include <mgfx.h>

#include "physics/math/v64_math.h"
#include "scene3d/v64_lighting.h"


static Light light;

Light *light_get(void) { return &light; }


void light_init(const LightDef *def)
{
	light = *def;

	for (int i = 0; i < LIGHT_COUNT; i++) {
		if (light.source[i].type == LIGHT_NONE) break;
		if (light.source[i].type == LIGHT_DIRECTIONAL)
			vector3_normalize(&light.source[i].directional.direction);
	}
}

void light_set(const Light *light, const Matrix4 *view, const mg_uniform_t *uniform)
{
	mgfx_light_parms_t lights[LIGHT_COUNT];

	int count = 0;
	for (; count < LIGHT_COUNT; count++) {
		const LightSource *source = &light->source[count];
		if (source->type == LIGHT_NONE) break;

		if (source->type == LIGHT_DIRECTIONAL) {
			/* w = 0: a direction, rotated into view space untranslated. */
			const Vector3 *d = &source->directional.direction;
			fm_vec4_t view_dir;
			fm_mat4_mul_vec4(&view_dir, view, &(fm_vec4_t){{d->x, d->y, d->z, 0.0f}});

			lights[count] = (mgfx_light_parms_t){
				.position = view_dir,
				.color = source->color,
			};
		} else {
			/* w = 1: a position, carried into view space. */
			const Vector3 *p = &source->point.position;
			fm_vec4_t view_pos;
			fm_mat4_mul_vec3(&view_pos, view, &(fm_vec3_t){{p->x, p->y, p->z}});

			lights[count] = (mgfx_light_parms_t){
				.position = {{view_pos.x, view_pos.y, view_pos.z, 1.0f}},
				.color = source->color,
				.intensity = source->point.size,
			};
		}
	}

	mgfx_set_lighting_inline(uniform, &(mgfx_lighting_parms_t){
		.ambient_color = light->ambient_color,
		.lights = lights,
		.light_count = count,
	});
}
