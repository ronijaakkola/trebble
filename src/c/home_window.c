#include <pebble.h>
#include "home_window.h"
#include "main_window.h"
#include "pins.h"
#include "pins_window.h"

static Window *homeWindow;
static MenuLayer *homeMenuLayer;
static StatusBarLayer *statusLayer;

// Home menu items. Subtitle is optional; an empty subtitle renders a single
// vertically centered title (like "Nearby stops"). Icons will be added later.
struct HomeItem {
	char title[20];
	char subtitle[20];
};

#define NUM_HOME_ITEMS 2
#define HOME_ROW_NEARBY 0
#define HOME_ROW_PINNED 1

static struct HomeItem home_items[NUM_HOME_ITEMS] = {
	{ "Nearby stops", "" },
	{ "Pinned stops", "" }, // subtitle is filled in at draw time from the live count
};

uint16_t home_menu_get_num_sections_callback(MenuLayer *menu_layer, void *data)
{
	return 1;
}

uint16_t home_menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
	switch (section_index) {
		case 0:
			return NUM_HOME_ITEMS;
		default:
			return 0;
	}
}

int16_t home_menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
	return MENU_CELL_BASIC_HEADER_HEIGHT;
}

void home_menu_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data)
{
	if (section_index != 0) {
		return;
	}
	GRect bounds = layer_get_bounds(cell_layer);
	graphics_context_set_text_color(ctx, GColorBlack);
	graphics_draw_text(ctx, "Trebble", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

int16_t home_menu_get_cell_height_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	return 48; // Pebble-style menu row height, matching the stops list.
}

void home_menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data)
{
	if (cell_index->section != 0) {
		return;
	}

	struct HomeItem *item = &home_items[cell_index->row];
	GRect bounds = layer_get_bounds(cell_layer);

	// The Pinned stops subtitle reflects how many stops are currently pinned.
	char pin_subtitle[20];
	const char *subtitle = item->subtitle;
	if (cell_index->row == HOME_ROW_PINNED) {
		int count = pins_count();
		snprintf(pin_subtitle, sizeof(pin_subtitle), "%d stops", count);
		subtitle = pin_subtitle;
	}

	#ifdef PBL_COLOR
		GColor title_color = GColorBlack;
		GColor subtitle_color = GColorDarkGray;
	#else
		GColor title_color = menu_cell_layer_is_highlighted(cell_layer) ? GColorWhite : GColorBlack;
		GColor subtitle_color = title_color;
	#endif

	int16_t text_x = 8;
	int16_t text_w = bounds.size.w - text_x - 4;
	int16_t cy = bounds.size.h / 2;

	graphics_context_set_text_color(ctx, title_color);

	if (subtitle[0] != '\0') {
		// Title above the midline, subtitle below it.
		graphics_draw_text(ctx, item->title, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(text_x, cy - 21, text_w, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
		graphics_context_set_text_color(ctx, subtitle_color);
		graphics_draw_text(ctx, subtitle, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(text_x, cy + 1, text_w, 18), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
	} else {
		// No subtitle: vertically center the title.
		graphics_draw_text(ctx, item->title, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(text_x, cy - 13, text_w, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
	}
}

void home_menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	switch (cell_index->row) {
		case HOME_ROW_NEARBY:
			window_stack_push(main_window_get_window(), true);
			break;
		case HOME_ROW_PINNED:
			pins_window_show();
			break;
		default:
			break;
	}
}

void home_setup_menu_layer(Window *window, Layer *window_layer)
{
	GRect window_bounds = layer_get_bounds(window_layer);
	window_bounds.origin.y = STATUS_BAR_LAYER_HEIGHT;

	homeMenuLayer = menu_layer_create(window_bounds);
	// Pebble Round has no room for a section header, so it is left off there.
	menu_layer_set_callbacks(homeMenuLayer, NULL, (MenuLayerCallbacks){
		#ifdef PBL_RECT
		 .get_num_sections = home_menu_get_num_sections_callback,
		 .get_header_height = home_menu_get_header_height_callback,
		 .draw_header = home_menu_draw_header_callback,
		#endif
		.get_num_rows = home_menu_get_num_rows_callback,
		.get_cell_height = home_menu_get_cell_height_callback,
		.draw_row = home_menu_draw_row_callback,
		.select_click = home_menu_select_callback,
	});

	menu_layer_set_highlight_colors(homeMenuLayer, COLOR_FALLBACK(HOME_HL_COLOR, GColorBlack), COLOR_FALLBACK(GColorBlack, GColorWhite));
	menu_layer_set_click_config_onto_window(homeMenuLayer, window);

	layer_add_child(window_layer, menu_layer_get_layer(homeMenuLayer));
}

void home_window_load(Window *window)
{
	Layer *window_layer = window_get_root_layer(window);

	home_setup_menu_layer(window, window_layer);

	statusLayer = status_bar_layer_create();
	status_bar_layer_set_separator_mode(statusLayer, StatusBarLayerSeparatorModeDotted);
	status_bar_layer_set_colors(statusLayer, GColorClear, GColorBlack);
	layer_add_child(window_layer, status_bar_layer_get_layer(statusLayer));
}

// Redraw on reveal so the Pinned stops count is up to date after the user pins
// or unpins stops elsewhere.
void home_window_appear(Window *window)
{
	if (homeMenuLayer) {
		menu_layer_reload_data(homeMenuLayer);
	}
}

void home_window_unload(Window *window)
{
	menu_layer_destroy(homeMenuLayer);
	status_bar_layer_destroy(statusLayer);
}

void home_window_create()
{
	homeWindow = window_create();
	window_set_window_handlers(homeWindow, (WindowHandlers) {
		.load = home_window_load,
		.appear = home_window_appear,
		.unload = home_window_unload
	});
}

void home_window_destroy()
{
	window_destroy(homeWindow);
}

Window *home_window_get_window()
{
	return homeWindow;
}
