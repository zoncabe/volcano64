/*
	Content declaration: a model plus what its kind needs, behind a tag.
	Position, rotation and scale are not here, they belong to the placement,
	so one prefab can be placed any number of times.
*/
#ifndef VOLCANO_64_PREFAB_H
#define VOLCANO_64_PREFAB_H

#include <stdint.h>

#include "entity/v64_entity.h"
#include "physics/body/v64_rigid_body.h"
#include "physics/cloth/v64_cloth.h"
#include "character/v64_character.h"
#include "shaders/v64_water.h"
#include "sound/v64_prefab_sound.h"


typedef enum {

	PREFAB_CHARACTER,
	PREFAB_PROP,
	PREFAB_CLOTH,
	PREFAB_WATER,

} PrefabType;


typedef struct Prefab {

	PrefabType type;
	const char *model;

	const PrefabSound *sound;
	uint8_t sound_count;

	/* Solid for a prop, sensor volume for water. NULL: no collision. */
	const EntityColliderDef *collider;

	/* The kind the tag names. A prop without a body is static. */
	union {
		const CharacterDef *character;
		const RigidBodyDef *prop;
		const ClothDef     *cloth;
		const WaterDef     *water;
	};

} Prefab;


#endif
