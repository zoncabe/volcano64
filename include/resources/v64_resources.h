#ifndef VOLCANO_64_RESOURCES_H
#define VOLCANO_64_RESOURCES_H

#include <stdint.h>
#include "graphics/v64_sprites.h"
#include "graphics/v64_font.h"


typedef struct {

	const SpriteID *sprite;
	uint8_t         sprite_count;
	const uint8_t  *font;
	uint8_t         font_count;

} ResourceSet;


void resources_load(const ResourceSet *set);
void resources_unload(const ResourceSet *set);

#endif
