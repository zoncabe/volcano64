#include <assert.h>
#include <string.h>
#include <stddef.h>

#include <magma.h>
#include <mgfx.h>
#include "physics/math/v64_math.h"
#include "animation/v64_armature.h"

#include "scene3d/v64_lighting.h"
#include "scene3d/v64_fog.h"
#include "viewport/v64_viewport.h"
#include "graphics/v64_font.h"
#include "graphics/v64_sprites.h"
#include "graphics/v64_shapes.h"
#include "particles/v64_particles.h"
#include "render/v64_render.h"
#include "scene2d/v64_scene2d.h"
#include "debug/v64_debug.h"
#include "time/v64_time.h"
#include "scene3d/v64_scene3d.h"

#include "game/v64_game.h"


void renderTransform_init(RenderTransform *t)
{
	*t = (RenderTransform){
		.position = {0.0f, 0.0f, 0.0f},
		.rotation = {0.0f, 0.0f, 0.0f},
		.scale    = {1.0f, 1.0f, 1.0f},
	};
}


/* The pipeline is the vertex stage: the mgfx shader patched to the
   renderer's vertex layout. The uniform handles address its DMEM regions. */
static mg_pipeline_t *render_pipeline;
static const mg_uniform_t *render_uniform_matrices;
static const mg_uniform_t *render_uniform_texturing;
static const mg_uniform_t *render_uniform_lighting;
static const mg_uniform_t *render_uniform_fog;

void render_init(void)
{
	mg_vertex_attribute_t attributes[] = {
		{ .input = MGFX_ATTRIBUTE_POSITION, .offset = offsetof(RenderVertex, position) },
		{ .input = MGFX_ATTRIBUTE_NORMAL,   .offset = offsetof(RenderVertex, normal)   },
		{ .input = MGFX_ATTRIBUTE_COLOR,    .offset = offsetof(RenderVertex, rgba)     },
		{ .input = MGFX_ATTRIBUTE_TEXCOORD, .offset = offsetof(RenderVertex, st)       },
	};

	render_pipeline = mg_pipeline_create(&(mg_pipeline_parms_t){
		.vertex_shader_ucode = mgfx_get_shader_ucode(0),
		.vertex_layout.attribute_count = sizeof(attributes) / sizeof(attributes[0]),
		.vertex_layout.attributes = attributes,
		.vertex_layout.stride = sizeof(RenderVertex),
	});

	render_uniform_matrices  = mg_pipeline_get_uniform(render_pipeline, MGFX_BINDING_MATRICES);
	render_uniform_texturing = mg_pipeline_get_uniform(render_pipeline, MGFX_BINDING_TEXTURING);
	render_uniform_lighting  = mg_pipeline_get_uniform(render_pipeline, MGFX_BINDING_LIGHTING);
	render_uniform_fog       = mg_pipeline_get_uniform(render_pipeline, MGFX_BINDING_FOG);

	model_setTexturingUniform(render_uniform_texturing);
	model_setMatricesUniform(render_uniform_matrices);
}


/* Final matrices for one draw, composed on the CPU: the ucode has no matrix
   stack. The normal matrix is the model-view, which assumes uniform-ish
   scaling. The ucode multiplies the raw s16 positions as plain integers
   (render units), same as tiny3d — no extra factor anywhere. */
static void render_loadMatrices(const Viewport *viewport, const Matrix4 *model)
{
	Matrix4 mvp, mv;

	if (model) {
		matrix4_product(&mvp, &viewport->view_projection, model);
		matrix4_product(&mv, &viewport->view, model);
	} else {
		mvp = viewport->view_projection;
		mv  = viewport->view;
	}

	mgfx_set_matrices_inline(render_uniform_matrices, &(mgfx_matrices_parms_t){
		.model_view_projection = mvp.m[0],
		.model_view            = mv.m[0],
		.normal                = mv.m[0],
	});
}

void render_initContext(RenderContext *ctx)
{
	/* Only the counts matter: entries are fully written before being read,
	   and zeroing the whole struct wipes the entire 8 KB dcache. */
	ctx->element_count = 0;
	ctx->section_count = 0;
	ctx->object_count  = 0;
}


static RenderSection *render_beginSection(RenderContext *ctx)
{
	RenderSection *section = &ctx->section[ctx->section_count++];
	section->element_start = ctx->element_count;
	section->element_count = 0;
	section->has_scissor   = false;
	return section;
}

static void render_endSection(RenderContext *ctx, RenderSection *section)
{
	section->element_count = ctx->element_count - section->element_start;
}

/* The visibility ran before the context: each mesh already wrote its own
   flags against the frame's frustum. Here they are only consumed. */
static void render_setScene3DContext(RenderContext *ctx, const Scene3D *s, const Viewport *viewport)
{
	uint8_t fb_index = viewport->fb_index;

	for (int i = 0; i < s->entity_count; i++) {
		Entity    *e      = s->entity[i];
		Mesh *mesh  = e->mesh;
		Matrix4  *matrix = mesh->matrix_buffer ? &mesh->matrix_buffer[fb_index] : NULL;
		Armature *skel  = mesh->skeleton;

		/* The mesh culls itself and writes its own flags; here they are
		   only consumed. */
		if (e->cull) {
			mesh_cull(mesh, viewport);
			if (mesh->culled) continue;
		}

		/* Whatever drives this mesh has already moved: fold the new positions
		   into this frame's vertex buffer, then point the segment its recorded
		   display list reads from at that same copy. */
		mesh_updateDeform(mesh, fb_index);
		mesh_bindDeformFrame(mesh, fb_index);

		/* A skinned mesh's pose is in its bones by now: write this frame's
		   palette, which is what its recorded block reads. */
		mesh_updatePalette(mesh, viewport, fb_index);

		if (mesh->dl_count == 0) {
			assert(ctx->object_count < RENDER_MAX_3D_ELEMENTS);
			ctx->object[ctx->object_count++] = (Element3D){ NULL, mesh->model, matrix, skel, mesh->draw_conf };
			continue;
		}

		for (int part = 0; part < mesh->dl_count; part++) {
			if (!(mesh->visible & (1u << part))) continue;

			assert(ctx->object_count < RENDER_MAX_3D_ELEMENTS);
			ctx->object[ctx->object_count++] = (Element3D){ mesh_partBlock(mesh, part, fb_index), NULL, matrix, skel };
		}
	}
}

/* One section per layer: the elements come from the live scene, the scissor
   from the definition that built it. */
static void render_setScene2DContext(RenderContext *ctx, const Scene2D *scene2d)
{
	if (!scene2d->def) return;

	for (int i = 0; i < scene2d->def->layer_count; i++) {
		const Scene2DLayer *src = &scene2d->def->layer[i];

		RenderSection *dst = render_beginSection(ctx);
		memcpy(ctx->element + ctx->element_count,
			   scene2d->element + scene2d->layer_start[i],
			   src->element_count * sizeof(Element2D));
		ctx->element_count += src->element_count;
		render_endSection(ctx, dst);

		dst->has_scissor = src->has_scissor;
		dst->scissor_x   = src->scissor_x;
		dst->scissor_y   = src->scissor_y;
		dst->scissor_w   = src->scissor_w;
		dst->scissor_h   = src->scissor_h;
	}
}

void render_setContext(RenderContext *ctx, const Scene3D *scene3d, const Viewport *viewport, const Scene2D *scene2d)
{
	render_initContext(ctx);
	if (scene3d) render_setScene3DContext(ctx, scene3d, viewport);
	if (scene2d) render_setScene2DContext(ctx, scene2d);
}

static void render_start(int *fb_index)
{
	*fb_index = (*fb_index + 1) % FB_COUNT;

	/* Geometry fades toward the fog color, so the background must be it. */
	Fog *fog = fog_get();
	viewport_clear(fog->enabled ? fog->color : RGBA32(0, 0, 0, 0xFF));

	/* Base render state for the frame (what t3d_frame_start used to set):
	   materials patch their raw othermodes over this. */
	rdpq_mode_begin();
		rdpq_set_mode_standard();
		rdpq_mode_antialias(AA_STANDARD);
		rdpq_mode_zbuf(true, true);
		rdpq_mode_persp(true);
		rdpq_mode_filter(FILTER_BILINEAR);
		rdpq_mode_dithering(DITHER_SQUARE_SQUARE);
		rdpq_mode_fog(0);
	rdpq_mode_end();
}

static void render_end(void)
{
	rdpq_detach_show();
}

/* Uniform fade for textured elements, the stamina wheel way: env alpha
   modulates only the alpha channel while RGB passes TEX0 untouched, so
   prim color stays free for tinting. */
static void render_setTransparency(const Element2D *element)
{
	if (element->transparency == 0) return;

	rdpq_set_env_color(RGBA32(0, 0, 0, 255 - element->transparency));
	rdpq_mode_combiner(RDPQ_COMBINER1((0,0,0,TEX0), (TEX0,0,ENV,0)));
}

void render(RenderContext *ctx, int *fb_index)
{
	render_start(fb_index);

	if (ctx->object_count > 0) {
		const Viewport *viewport = viewport_get();

		mg_pipeline_bind(render_pipeline);
		mg_set_viewport(&viewport->screen);
		mg_set_culling(&(mg_culling_parms_t){ .cull_mode = MG_CULL_MODE_BACK });
		mg_set_geometry_flags(MG_GEOMETRY_FLAGS_Z_ENABLED
		                    | MG_GEOMETRY_FLAGS_TEX_ENABLED
		                    | MG_GEOMETRY_FLAGS_SHADE_ENABLED);

		light_set(light_get(), &viewport->view, render_uniform_lighting);
		fog_set(fog_get(), render_uniform_fog);

		/* Baseline for materials whose combiner reads PRIM or ENV without
		   setting them: white passes through. The RDP boots them undefined,
		   and the 2D pass leaves its own values behind each frame. */
		rdpq_set_prim_color(RGBA32(0xFF, 0xFF, 0xFF, 0xFF));
		rdpq_set_env_color(RGBA32(0xFF, 0xFF, 0xFF, 0xFF));

		/* A mesh contributes one element per visible part, and every part of
		   the same mesh shares its matrix: a character with three weapons is
		   four elements with identical state. Loading the matrices once per
		   run of equal state cuts that setup without altering a single draw. */
		const Matrix4 *loaded = NULL;
		bool first = true;

		for (int i = 0; i < ctx->object_count; i++) {
			Element3D *obj = &ctx->object[i];

			if (first || obj->matrix != loaded) {
				render_loadMatrices(viewport, obj->matrix);
				loaded = obj->matrix;
				first = false;
			}

			if (obj->dl) {
				rspq_block_run(obj->dl);
				/* A skinned block loads the uniform per bone batch, so what
				   is in it afterwards is the last bone, not the mesh: the
				   next element reloads whatever it needs. */
				if (obj->skeleton) first = true;
				continue;
			}

			ModelState state = model_stateCreate();
			state.drawConf = obj->conf;
			ModelIter it = model_iterCreate(obj->model, CHUNK_TYPE_OBJECT);
			while (model_iterNext(&it)) {
				if (!it.object->isVisible) continue;
				model_drawMaterial(it.object->material, &state);
				rspq_block_run(it.object->userBlock);
			}
		}
	}

	particles_draw();

	for (int s = 0; s < ctx->section_count; s++) {
		RenderSection *section = &ctx->section[s];

		/* Nothing on screen, nothing on the wire: a fully hidden section
		   must not emit a single command, scissor setup included. */
		bool any_visible = false;
		for (int i = 0; i < section->element_count; i++) {
			if (!ctx->element[section->element_start + i].is_hidden) {
				any_visible = true;
				break;
			}
		}
		if (!any_visible) continue;

		if (section->has_scissor) {
			rdpq_set_scissor(
				section->scissor_x,
				section->scissor_y,
				section->scissor_x + section->scissor_w,
				section->scissor_y + section->scissor_h);
		}

		for (int i = 0; i < section->element_count; i++) {
			Element2D *element = &ctx->element[section->element_start + i];
			if (element->is_hidden) continue;
			switch (element->type) {
				case ELEMENT2D_RECTANGLE:    shape_drawRectangle(&element->rectangle, element->position, element->scale);                          break;
				case ELEMENT2D_TEXT:         text_draw(&element->text, element->position);                                                         break;
				case ELEMENT2D_SPRITE:       sprite_setMode(); render_setTransparency(element); sprite_draw(&element->sprite, element->position, element->scale, element->rotation); break;
				case ELEMENT2D_TILED_SPRITE: sprite_setMode(); render_setTransparency(element); sprite_drawTiled(&element->sprite, element->position, element->scale);               break;
			}
		}

		if (section->has_scissor)
			rdpq_set_scissor(0, 0, 320, 240);
	}

	debugUI_draw();

	render_end();
}
