#include <assert.h>
#include <malloc.h>
#include <string.h>
#include <libdragon.h>
#include <fgeom.h>
#include "animation/v64_model.h"
#include "animation/v64_armature.h"

#include "entity/v64_entity.h"
#include "character/v64_character.h"
#include "character/v64_character_animation.h"


/* SkeletonModifierFn: the weapon posing; context is the Character. */
static void character_weaponModifier(Armature *skeleton, void *context)
{
	(void)skeleton;
	characterWeapon_setBones(context);
}

Character *character_create(const CharacterDef *def, Entity *entity)
{
	/* The spring bone states ride in the same allocation; their only
	   references are the modifier contexts, freed with the character. */
	uint8_t spring_bones = 0;
	if (def->spring_bones)
		for (const SpringBonesDef *set = def->spring_bones; set->count; set++)
			spring_bones += set->count;

	Character *character = malloc(sizeof(Character) + spring_bones * sizeof(SpringBone));
	assert(character);

	*character = (Character){
		.entity    = entity,
		.stats     = (CharacterStats){ .settings = def->stats_settings, .stamina = 1.0f },
		.body      = (KinematicBody){ .position = vector3_scaled(&entity->transform.position, RENDER_SCALE_INV), .rotation = entity->transform.rotation },
		.movement  = (CharacterMovement){ .settings = def->movement_settings, .data.is_grounded = true, .current = MOVEMENT_STATE_IDLE },
		.animation = (CharacterAnimation){ .def = def->animation_def },
		.weapons   = (CharacterWeapons){ .def = def->weapons_def, .drawn = CHARACTER_WEAPON_DRAWN_NONE },
		/* No previous frame to compare against yet: a cycle of -1 crosses
		   nothing, and the body starts standing on the floor. */
		.sound     = (CharacterSound){ .def = def->sound_def, .previous_cycle = -1.0f, .previous_grounded = true },
	};

	characterCollider_init(&character->collider,
		def->collider_settings->radius,
		(def->collider_settings->height - 2.0f * def->collider_settings->radius) * 0.5f);
	characterCollider_setVertical(&character->collider, &character->body.position);

	/* A def without animations (a vehicle) skips the whole graph: the mesh
	   keeps a NULL skeleton and draws through the model object path. */
	if (def->animation_def) {
		characterAnimation_initGraph(character, def->animation_def);
		entity->mesh->skeleton = &character->animation.main;
	}

	/* Aim before the weapons: the bow has to follow a spine already bent. */
	if (def->aiming_settings) {
		characterAim_init(character, def->aiming_settings);
		skeletonModifiers_add(&character->skeleton_modifiers, characterAim_apply, character);
	}

	skeletonModifiers_add(&character->skeleton_modifiers, character_weaponModifier, character);

	if (spring_bones > 0) {
		SpringBone *spring_bone = (SpringBone *)(character + 1);
		uint8_t n = 0;

		/* Chains rely on this order: the resolved joints run root to tip,
		   so each modifier runs after the one it hangs from. */
		for (const SpringBonesDef *set = def->spring_bones; set->count; set++) {
			int16_t joint[16];
			uint8_t count = springBones_resolveChain(&character->animation.main, set, joint, 16);
			if (count > set->count) count = set->count;

			for (uint8_t i = 0; i < count; i++) {
				if (!springBone_init(&spring_bone[n], &character->animation.main, joint[i],
				                     i, set, &entity->transform))
					continue;

				skeletonModifiers_add(&character->skeleton_modifiers, springBone_apply, &spring_bone[n]);
				n++;
			}
		}
	}

	/* Part 0 = body, parts 1..N = one per weapon object, def order.
	   Only the body starts visible; equipping turns weapon bits on.
	   No weapons: the whole model is the single skinned part. No skeleton
	   either: per-object blocks, exactly what a prop gets. The skeleton is
	   already on the mesh, so the parts record against its bone palette. */
	if (def->weapons_def)
		mesh_recordParts(entity->mesh, def->weapons_def->mesh, def->weapons_def->mesh_count);
	else if (def->animation_def)
		mesh_recordParts(entity->mesh, NULL, 0);
	else
		mesh_recordObjects(entity->mesh);

	return character;
}

void character_getBoneModelSpacePose(const Armature *skeleton, int16_t bone, Vector3 *position, Quaternion *rotation)
{
	uint16_t chain[16];
	int depth = 0;

	uint16_t idx = (uint16_t)bone;
	while (idx != 0xFFFF && depth < 16) {
		chain[depth++] = idx;
		idx = skeleton->skeletonRef->bones[idx].parentIdx;
	}

	*position = (Vector3){ 0.0f, 0.0f, 0.0f };
	*rotation = (Quaternion){ 0.0f, 0.0f, 0.0f, 1.0f };

	for (int i = depth - 1; i >= 0; i--) {
		const Bone *b = &skeleton->bones[chain[i]];

		Vector3 step = quaternion_rotateVector(rotation, &b->position);
		position->x += step.x;
		position->y += step.y;
		position->z += step.z;

		*rotation = quaternion_product(rotation, &b->rotation);
	}
}

/* Model-space pose of a bone, composed from the local TRS chain so it is
   current-frame (bone->matrix would lag one skeleton update behind). */
void character_getBonePose(const Armature *skeleton, int16_t bone, Vector3 *position, Quaternion *rotation)
{
	uint16_t chain[16];
	int depth = 0;

	uint16_t idx = (uint16_t)bone;
	while (idx != 0xFFFF && depth < 16) {
		chain[depth++] = idx;
		idx = skeleton->skeletonRef->bones[idx].parentIdx;
	}

	*position = (Vector3){ 0.0f, 0.0f, 0.0f };
	*rotation = (Quaternion){ 0.0f, 0.0f, 0.0f, 1.0f };

	for (int i = depth - 1; i >= 0; i--) {
		const Bone *b = &skeleton->bones[chain[i]];

		Vector3 step = quaternion_rotateVector(rotation, &b->position);
		position->x += step.x;
		position->y += step.y;
		position->z += step.z;

		*rotation = quaternion_product(rotation, &b->rotation);
	}
}

void character_delete(Character *character)
{
	CharacterAnimation *animation = &character->animation;

	if (animation->def) {
		for (int i = 0; i < animation->def->clip_count; i++) {
			animation_destroy(&animation->clip[i]);
			if (animation->clip_data[i]) free(animation->clip_data[i]);
		}
		for (int i = 0; i < animation->def->buffer_count; i++)
			armature_destroy(&animation->buffer[i]);
		armature_destroy(&animation->main);
	}

	free(animation->clip);
	free(animation->clip_data);
	free(animation->clip_cooldown);
	free(animation->buffer);
	free(animation->node_state);
	free(animation->node_active);
	free(character);
}
