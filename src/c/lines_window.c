#include <pebble.h>
#include "lines_window.h"
#include "l_loading_window.h"
#include "error_window.h"

static Window *linesWindow;
static MenuLayer *lineMenuLayer;
static StatusBarLayer *statusLayer;

char stopName[30];
static int lineAmount = 0;
static struct LineInfo lines[NUM_LINES];

void lines_window_update_lines(struct LineInfo newLines[NUM_LINES], int amount, char *name)
{
	// Update lines
	for(int i = 0; i < NUM_LINES; ++i) {
		lines[i] = newLines[i];
	}
	lineAmount = amount;
	strncpy(stopName, name, 30);
}

uint16_t lines_get_num_sections_callback(MenuLayer *menu_layer, void *data)
{
	return 1;
}

uint16_t lines_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
	switch (section_index) {
		case 0:
			return lineAmount;
		default:
			return 0;
	}
}

int16_t lines_get_cell_height_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	if(cell_index->section == 0) {
		#if defined(PBL_ROUND)
			return 74;
		#elif defined(PBL_PLATFORM_EMERY)
			return 64;
		#else
			return 60; // Tall enough to pad the badge and destination top/bottom
		#endif
	}
	else return 44; //Default height of cell
}

int16_t lines_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
	return MENU_CELL_BASIC_HEADER_HEIGHT;
}

void lines_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data)
{
	switch (section_index) {
		case 0:
			menu_cell_basic_header_draw(ctx, cell_layer, (char *)stopName);
			break;
	}
}

// Picks the badge background color for a line type. On color watches buses are
// blue and trams red; on black-and-white watches both fall back to black, where
// the type is not distinguished.
static GColor badge_color_for_type(const char *type)
{
	if (type[0] == 'B') {
		return COLOR_FALLBACK(GColorCobaltBlue, GColorBlack);
	}
	if (type[0] == 'T') {
		return COLOR_FALLBACK(GColorRed, GColorBlack);
	}
	// Unknown mode: neutral badge so the line code stays readable.
	return GColorBlack;
}

#ifdef PBL_ROUND
static int16_t code_badge_width(GContext *ctx, const char *code, GFont font)
{
	GSize text = graphics_text_layout_get_content_size(code, font, GRect(0, 0, 90, 40), GTextOverflowModeWordWrap, GTextAlignmentLeft);
	return text.w + 10;
}
#endif

// Draws the line code inside a rounded, type-colored badge at (x, y).
static void draw_code_badge(GContext *ctx, const char *code, const char *type, int16_t x, int16_t y, GFont font)
{
	GSize text = graphics_text_layout_get_content_size(code, font, GRect(0, 0, 90, 40), GTextOverflowModeWordWrap, GTextAlignmentLeft);
	int16_t bw = text.w + 12;
	int16_t bh = text.h + 2;

	graphics_context_set_fill_color(ctx, badge_color_for_type(type));
	graphics_fill_rect(ctx, GRect(x, y, bw, bh), 4, GCornersAll);

	graphics_context_set_text_color(ctx, GColorWhite);
	// Pebble draws the glyph near the top of its box with descender space below,
	// so a number (no descenders) looks off-center. Nudge it to center it vertically.
	graphics_draw_text(ctx, code, font, GRect(x + 6, y - 3, bw, bh + 4), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

void lines_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data)
{
	if (cell_index->section != 0) {
		return;
	}

	struct LineInfo *line = &lines[cell_index->row];
	// On color watches the selection is light gray, so text stays black. On
	// black-and-white watches the selection is black, so highlighted text is white.
	#ifdef PBL_COLOR
		GColor text_color = GColorBlack;
	#else
		GColor text_color = menu_cell_layer_is_highlighted(cell_layer) ? GColorWhite : GColorBlack;
	#endif

	// Take Round and Pebble 2 into account
	#ifdef PBL_ROUND
		GFont code_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
		int16_t bw = code_badge_width(ctx, line->code, code_font);
		draw_code_badge(ctx, line->code, line->type, (180 - bw) / 2, 6, code_font);
		graphics_context_set_text_color(ctx, text_color);
		graphics_draw_text(ctx, line->dir, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(0, 30, 180, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
		graphics_draw_text(ctx, line->time, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(0, 50, 180, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
	#elif PBL_PLATFORM_EMERY
		draw_code_badge(ctx, line->code, line->type, 4, 6, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
		graphics_context_set_text_color(ctx, text_color);
		graphics_draw_text(ctx, line->dir, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(4, 34, 150, 24), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
		graphics_draw_text(ctx, line->time, fonts_get_system_font(FONT_KEY_GOTHIC_24), GRect(135, 0, 62, 24), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
	#else
		draw_code_badge(ctx, line->code, line->type, 4, 6, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
		graphics_context_set_text_color(ctx, text_color);
		graphics_draw_text(ctx, line->dir, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(4, 34, 136, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
		graphics_draw_text(ctx, line->time, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(78, 0, 62, 21), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
	#endif
}

void lines_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	return;
}

void setup_lines_layer(Window *window, Layer *window_layer)
{
	GRect window_bounds = layer_get_bounds(window_layer);
	window_bounds.origin.y = STATUS_BAR_LAYER_HEIGHT;

	lineMenuLayer = menu_layer_create(window_bounds);
	menu_layer_set_callbacks(lineMenuLayer, NULL, (MenuLayerCallbacks){
		#ifdef PBL_RECT
		 .get_num_sections = lines_get_num_sections_callback,
		 .get_header_height = lines_get_header_height_callback,
		 .draw_header = lines_draw_header_callback,
		#endif
		.get_num_rows = lines_get_num_rows_callback,
		.get_cell_height = lines_get_cell_height_callback,
		.draw_row = lines_draw_row_callback,
		.select_click = lines_select_callback,
	});

	menu_layer_set_highlight_colors(lineMenuLayer, COLOR_FALLBACK(LINES_HL_COLOR, GColorBlack), COLOR_FALLBACK(GColorBlack, GColorWhite));
	menu_layer_set_click_config_onto_window(lineMenuLayer, window);

	layer_add_child(window_layer, menu_layer_get_layer(lineMenuLayer));
}

void lines_window_load(Window *window)
{
	Layer *window_layer = window_get_root_layer(window);
	setup_lines_layer(window, window_layer);

	statusLayer = status_bar_layer_create();
  status_bar_layer_set_separator_mode(statusLayer, StatusBarLayerSeparatorModeDotted);
  status_bar_layer_set_colors(statusLayer, GColorClear, GColorBlack);
	layer_add_child(window_layer, status_bar_layer_get_layer(statusLayer));
}

void lines_window_unload(Window *window)
{
	menu_layer_destroy(lineMenuLayer);
	status_bar_layer_destroy(statusLayer);
}

void lines_window_create()
{
	linesWindow = window_create();
	window_set_window_handlers(linesWindow, (WindowHandlers) {
		.load = lines_window_load,
		.unload = lines_window_unload
	});
}

void lines_window_destroy()
{
	window_destroy(linesWindow);
}

Window *lines_window_get_window()
{
	return linesWindow;
}
