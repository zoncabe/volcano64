#ifndef VOLCANO_64_CHARACTER_H
#define VOLCANO_64_CHARACTER_H

#include <stdbool.h>
#include "physics/math/v64_math.h"
#include "animation/v64_model.h"
#include "animation/v64_armature.h"
#include "animation/v64_animation.h"

#include "physics/v64_physics.h"
#include "graphics/v64_mesh.h"
#include "character/v64_character_physics.h"
#include "character/v64_character_movement.h"
#include "character/v64_character_stats.h"
#include "character/v64_character_animation.h"
#include "character/v64_character_weapon.h"
#include "character/v64_character_aim.h"
#include "character/v64_character_skeleton.h"
#include "character/v64_character_spring_bone.h"
#include "character/v64_character_sound.h"

typedef struct Entity Entity;

typedef struct CharacterDef {

	const CharacterMovementSettings *movement_settings;
	const CharacterAnimationDef *animation_def;
	const CharacterColliderSettings *collider_settings;
	const CharacterWeaponsDef *weapons_def;
	const SpringBonesDef *spring_bones;   /* optional: array of sets, one tuning each, count 0 terminates */
	const CharacterAimingSettings *aiming_settings;   /* optional: spine chain for the camera-pitch bend */
	const CharacterSoundDef *sound_def;
	const CharacterStatsSettings *stats_settings;

} CharacterDef;

typedef struct Character {

	Entity             *entity;
	KinematicBody       body;
	CharacterCollider   collider;
	CharacterMovement   movement;
	CharacterAnimation  animation;
	CharacterWeapons    weapons;
	CharacterAiming     aiming;
	CharacterSound      sound;
	SkeletonModifiers   skeleton_modifiers;
	CharacterStats      stats;

} Character;


Character *character_create(const CharacterDef *def, Entity *entity);
void character_delete(Character *character);

/* Model-space pose of a bone, composed from the local TRS chain so it is
   current-frame (bone->matrix would lag one skeleton update behind). */
void character_getBonePose(const Armature *skeleton, int16_t bone, Vector3 *position, Quaternion *rotation);


#endif
