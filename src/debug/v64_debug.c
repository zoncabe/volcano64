#include <stdio.h>
#include <stdarg.h>
#include <libdragon.h>

#include "debug/v64_debug.h"


#define DEBUG_UI_LINES     8
#define DEBUG_UI_LINE_MAX 40

#define DEBUG_UI_X    16.0f
#define DEBUG_UI_Y    24.0f
#define DEBUG_UI_STEP 10.0f


static char debug_line[DEBUG_UI_LINES][DEBUG_UI_LINE_MAX];
static bool debug_active;
static bool debug_show_fps;


void debugUI_init(void)
{
	rdpq_text_register_font(FONT_BUILTIN_DEBUG_MONO,
	                        rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO));
	debug_active = true;
}

void debugUI_show(bool show)
{
	debug_active = show;
}

void debugUI_set(uint8_t line, const char *fmt, ...)
{
	if (line >= DEBUG_UI_LINES) return;

	va_list args;
	va_start(args, fmt);
	vsnprintf(debug_line[line], DEBUG_UI_LINE_MAX, fmt, args);
	va_end(args);
}

/* The state asks here; the frame is not attached yet, so the drawing
   itself waits for debugUI_draw. The request lasts one frame. */
void debugUI_showFPS(void)
{
	debug_show_fps = true;
}

void debugUI_draw(void)
{
	if (!debug_active) return;

	for (int i = 0; i < DEBUG_UI_LINES; i++) {
		if (debug_line[i][0] == '\0') continue;
		rdpq_text_print(NULL, FONT_BUILTIN_DEBUG_MONO,
		                DEBUG_UI_X, DEBUG_UI_Y + i * DEBUG_UI_STEP,
		                debug_line[i]);
	}

	/* Top right, first line. */
	if (debug_show_fps) {
		rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 270.0f, DEBUG_UI_Y,
		                 "%.1f", display_get_fps());
		debug_show_fps = false;
	}
}
