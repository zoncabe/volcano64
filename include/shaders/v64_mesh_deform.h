/*
	Drives a t3d vertex buffer from an external set of points.

	Binds every vertex slot of a model to the source point sitting at the same
	rest position, then rewrites the buffer from those points on demand. The
	source is a plain array, so anything that moves points can drive a mesh:
	a collision mesh under simulation, a morph target, a spline.

	Binding by position rather than by index is what makes this work: t3d
	chunks, pads and reorders vertices when it builds the model, and glTF
	splits them again per UV seam and material border, so one source point
	usually lands on several slots and never on a predictable one.
*/
#ifndef VOLCANO_64_MESH_DEFORM_H
#define VOLCANO_64_MESH_DEFORM_H

#include <stdbool.h>
#include <stdint.h>
#include "animation/v64_model.h"

#include "viewport/v64_viewport.h"      /* FB_COUNT */
#include "physics/math/v64_vector3.h"


#define MESH_DEFORM_UNBOUND 0xFFFF

/* One vertex buffer per framebuffer, same reason the matrices are buffered. */
#define MESH_DEFORM_BUFFERS FB_COUNT

typedef struct MeshDeform {
	uint16_t      *slot_source;   /* source index per vertex slot, or MESH_DEFORM_UNBOUND */
	uint16_t       slot_count;    /* vertex slots in the model */
	uint16_t       bound_count;   /* slots that found a source point */
	float          scale;         /* source units -> render units */
	Model      *model;
	const Vector3 *source;        /* the points driving the mesh; kept, not owned */

	/* The RSP runs behind the CPU, so writing one buffer every frame lets it
	   read vertices half-overwritten. One copy per framebuffer, bound at
	   draw time, keeps the frame being drawn intact. */
	RenderVertex *vertex_buffer[MESH_DEFORM_BUFFERS];
	uint32_t       vertex_bytes;

	/* Optional: one normal per source point, in the same order. The source
	   decides its own winding, which may run against the model's, so the
	   binding measures the two at rest and keeps the sign that agrees. */
	const Vector3 *source_normal;
	float          normal_sign;

	/* Optional: one RGBA per source point, written into the vertex colors.
	   NULL leaves the model's own colors alone. */
	const uint8_t *source_rgba;
} MeshDeform;


/* Matches the model's rest pose against the source points; both must still be
   at rest. The source array is kept, so it has to outlive the binding and keep
   its order. Pass source_normal to drive the shading too, or NULL to leave the
   model's own normals alone. Returns false only if the binding could not be
   allocated; a partial match counts as success and leaves the unmatched slots
   frozen. */
bool meshDeform_bind(MeshDeform *deform, Model *model,
                     const Vector3 *source, const Vector3 *source_normal,
                     uint16_t source_count, float scale);

/* Writes the current source positions, and normals if bound, into this
   frame's vertex buffer. */
void meshDeform_apply(const MeshDeform *deform, uint8_t fb_index);

/* Selects this frame's buffer for the draw. Runs before the mesh's display
   list and outside it, since the block is recorded only once. */
void meshDeform_bindFrame(const MeshDeform *deform, uint8_t fb_index);

void meshDeform_delete(MeshDeform *deform);


#endif
