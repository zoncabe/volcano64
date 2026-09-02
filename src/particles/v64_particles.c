/*
	Particle buffers. The backend was tiny3d's tpx ucode; see the TODO in the
	header: everything below keeps the module's contract but draws nothing
	until a magma-era backend exists.
*/
#include <assert.h>
#include <malloc.h>
#include <libdragon.h>

#include "viewport/v64_viewport.h"
#include "physics/math/v64_matrix4.h"
#include "particles/v64_particles.h"


#define PARTICLES_MAX 8

/* byte sizes of one interleaved particle pair in the tpx layout */
#define PARTICLE_S8_PAIR_BYTES  16
#define PARTICLE_S16_PAIR_BYTES 24

static Particle particle[PARTICLES_MAX];
static uint8_t particle_count;


void particles_init(void)
{
}

Particle *particles_add(const Particle *def)
{
	assert(particle_count < PARTICLES_MAX);

	Particle *added = &particle[particle_count++];
	*added = *def;
	return added;
}

void particles_update(const GameContext *ctx, uint8_t fb_index)
{
	for (int i = 0; i < particle_count; i++)
		particle[i].update(&particle[i], ctx, fb_index);
}

void particles_draw(void)
{
}


ParticleBuffer particleBuffer_create(ParticleType type, uint32_t count)
{
	assert(count % 2 == 0);

	uint32_t pair_size = type == PARTICLE_S8 ? PARTICLE_S8_PAIR_BYTES : PARTICLE_S16_PAIR_BYTES;

	ParticleBuffer buffer = {
		.type   = type,
		.count  = count,
		.matrix = malloc_uncached(sizeof(mgfx_matrix_t) * FB_COUNT),
	};
	buffer.s8 = malloc_uncached(pair_size * count / 2);

	return buffer;
}

void particleBuffer_delete(ParticleBuffer *buffer)
{
	free_uncached(buffer->s8);
	free_uncached(buffer->matrix);
	*buffer = (ParticleBuffer){0};
}

void particleBuffer_setMatrix(ParticleBuffer *buffer, const float scale[3], const float rotation[3], const float position[3], uint8_t fb_index)
{
	Matrix4 matrix;
	matrix4_fromSrtEuler(&matrix,
		&(Vector3){scale[0], scale[1], scale[2]},
		&(Vector3){rotation[0], rotation[1], rotation[2]},
		&(Vector3){position[0], position[1], position[2]});
	matrix4_toFixed(&buffer->matrix[fb_index], &matrix);
}

void particleBuffer_draw(const ParticleBuffer *buffer, const mgfx_matrix_t *matrix)
{
	(void)buffer;
	(void)matrix;
}

void particleBuffer_drawTextured(const ParticleBuffer *buffer, const mgfx_matrix_t *matrix)
{
	(void)buffer;
	(void)matrix;
}
