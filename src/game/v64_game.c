#include "viewport/v64_viewport.h"
#include "time/v64_time.h"
#include "scene3d/v64_scene3d.h"
#include "render/v64_render.h"
#include "player/v64_player.h"
#include "control/v64_controller.h"
#include "control/v64_player_control.h"
#include "game/v64_game.h"
#include "game/v64_game_states.h"
#include "sound/v64_sound.h"
#include "menu/v64_settings.h"
#include "menu/v64_menu.h"
#include "particles/v64_particles.h"


static Game game;

static RenderContext render_context;


Game *game_get(void) { return &game; }

GameContext game_getContext(void)
{
	return (GameContext){
		.game     = &game,
		.viewport = viewport_get(),
		.scene3d  = scene3d_get(),
		.player   = player_get(),
	};
}

void game_init()
{
	asset_init_compression(2);

	dfs_init(DFS_DEFAULT_LOCATION);

	srand(getentropy32());
	
	rdpq_init();
	
	joypad_init();
	controller_start();
	
	time_init();

	viewport_init();

	render_init();

	particles_init();

	player_init();

	settings_init();

	menuStack_init();

	/* The game's own inits and game_start (state table, initial state)
	   run after this, from the game's main. */
}

void game_runStep(void)
{
	time_update();

	GameContext ctx = game_getContext();

	controller_poll();
	player_setControllerData(ctx.game);

	game_updateState(&ctx);

	sound_update();

	GameRenderDescriptor desc = game_getRenderDescriptor(&ctx);
	render_setContext(&render_context, desc.scene3d, ctx.viewport, desc.scene2d);
	render(&render_context, &ctx.viewport->fb_index);

	sound_poll();
}

void game_close()
{
	mg_close();
}
