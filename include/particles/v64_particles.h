#ifndef VOLCANO_64_PARTICLES_H
#define VOLCANO_64_PARTICLES_H

#include <stdbool.h>
#include <stdint.h>
#include "physics/math/v64_math.h"

/* TODO(magma): the particle backend was tiny3d's tpx ucode, which boots by
   copying the t3d ucode's state and cannot run without it. The module keeps
   its API but draws nothing until a magma-era backend replaces it (tpx
   ported standalone, or particles through the magma pipeline). */

/* Particles come interleaved in pairs, so a buffer always holds an even
   count. S8 keeps local coords in one byte for local effects; S16 covers a
   larger range for world placement. */
typedef enum {

	PARTICLE_S8,
	PARTICLE_S16,

} ParticleType;

typedef struct {

	ParticleType type;
	uint32_t count;

	union {
		void *s8;
		void *s16;
	};

	mgfx_matrix_t *matrix;   /* one per framebuffer */

} ParticleBuffer;


typedef struct GameContext GameContext;
typedef struct Particle Particle;

/* Dedicated input function: reads whatever drives the effect and fills
   visibility and this frame's matrix. */
typedef void (*ParticleUpdate)(Particle *particle, const GameContext *ctx, uint8_t fb_index);

/* rdpq state (combiner, textures) set right before the buffer is drawn. */
typedef void (*ParticleSetRenderState)(void);

struct Particle {

	ParticleBuffer buffer;
	ParticleUpdate update;
	ParticleSetRenderState set_render_state;
	bool textured;
	bool visible;
	const mgfx_matrix_t *matrix;   /* the one written this frame */

};


void particles_init(void);

Particle *particles_add(const Particle *def);
void particles_update(const GameContext *ctx, uint8_t fb_index);
void particles_draw(void);

ParticleBuffer particleBuffer_create(ParticleType type, uint32_t count);
void particleBuffer_delete(ParticleBuffer *buffer);
void particleBuffer_setMatrix(ParticleBuffer *buffer, const float scale[3], const float rotation[3], const float position[3], uint8_t fb_index);
void particleBuffer_draw(const ParticleBuffer *buffer, const mgfx_matrix_t *matrix);
void particleBuffer_drawTextured(const ParticleBuffer *buffer, const mgfx_matrix_t *matrix);

#endif
