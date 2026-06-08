#include <pebble.h>
#include "home_window.h"
#include "main_window.h"
#include "pins.h"
#include "pins_window.h"

static Window *homeWindow;
static MenuLayer *homeMenuLayer;
static StatusBarLayer *statusLayer;

// The highlighted row is remembered across the menu layer being freed (while a
// sub-window is on top) and rebuilt, so going back to the home menu returns to the
// row the user left rather than resetting to the top.
static uint16_t savedSelectedRow = 0;

// PDC row icons, one per home item. Loaded on window load and reused for every
// redraw, mirroring how the error window holds its PDC icon.
static GDrawCommandImage *nearbyIcon;
static GDrawCommandImage *pinnedIcon;

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

	#if defined(PBL_COLOR)
		GColor title_color = GColorBlack;
		GColor subtitle_color = GColorDarkGray;
	#elif defined(PBL_PLATFORM_APLITE)
		// Aplite is true 1-bit with no dithering, so invert the text on the
		// solid-black selected row.
		GColor title_color = menu_cell_layer_is_highlighted(cell_layer) ? GColorWhite : GColorBlack;
		GColor subtitle_color = title_color;
	#else
		// Diorite renders a dithered light-gray selection, so black text (and the
		// black row icons) stay legible whether or not the row is selected.
		GColor title_color = GColorBlack;
		GColor subtitle_color = GColorBlack;
	#endif

	// Row icon on the left, vertically centered like Pebble's own menu icons. The
	// title starts to the right of it.
	int16_t icon_left = 8;
	int16_t text_x = icon_left + 25 + 10; // fallback width if the PDC is missing
	GDrawCommandImage *icon = (cell_index->row == HOME_ROW_NEARBY) ? nearbyIcon : pinnedIcon;
	if (icon) {
		GSize icon_size = gdraw_command_image_get_bounds_size(icon);
		GPoint icon_origin = GPoint(icon_left, (bounds.size.h - icon_size.h) / 2);
		gdraw_command_image_draw(ctx, icon, icon_origin);
		text_x = icon_left + icon_size.w + 10;
	}

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

	// Aplite has no gray, so it keeps the classic inverted (black) selection;
	// every other platform (incl. diorite, which dithers) uses the light-gray
	// selection so the black row icons remain visible when a row is highlighted.
	#ifdef PBL_PLATFORM_APLITE
		menu_layer_set_highlight_colors(homeMenuLayer, GColorBlack, GColorWhite);
	#else
		menu_layer_set_highlight_colors(homeMenuLayer, HOME_HL_COLOR, GColorBlack);
	#endif
	menu_layer_set_click_config_onto_window(homeMenuLayer, window);

	layer_add_child(window_layer, menu_layer_get_layer(homeMenuLayer));
}

// Builds the home menu, its row icons and the status bar. Split out so it can run
// both on initial load and when the window is revealed again after being covered
// (see home_window_appear/disappear). No-op if already built.
static void home_build_ui(Window *window)
{
	if (homeMenuLayer) {
		return;
	}
	Layer *window_layer = window_get_root_layer(window);

	home_setup_menu_layer(window, window_layer);

	nearbyIcon = gdraw_command_image_create_with_resource(RESOURCE_ID_IMAGE_LOCATION);
	pinnedIcon = gdraw_command_image_create_with_resource(RESOURCE_ID_IMAGE_TIMELINE_PIN);

	statusLayer = status_bar_layer_create();
	status_bar_layer_set_separator_mode(statusLayer, StatusBarLayerSeparatorModeDotted);
	status_bar_layer_set_colors(statusLayer, GColorClear, GColorBlack);
	layer_add_child(window_layer, status_bar_layer_get_layer(statusLayer));
}

// Frees the home menu, its icons and status bar. The selected row is remembered
// first (see savedSelectedRow) so it can be restored when the menu is rebuilt;
// freeing the rest whenever another window is on top keeps aplite's small heap
// available for that window.
static void home_destroy_ui(void)
{
	if (homeMenuLayer) {
		savedSelectedRow = menu_layer_get_selected_index(homeMenuLayer).row;
		menu_layer_destroy(homeMenuLayer);
		homeMenuLayer = NULL;
	}
	if (statusLayer) {
		status_bar_layer_destroy(statusLayer);
		statusLayer = NULL;
	}
	if (nearbyIcon) {
		gdraw_command_image_destroy(nearbyIcon);
		nearbyIcon = NULL;
	}
	if (pinnedIcon) {
		gdraw_command_image_destroy(pinnedIcon);
		pinnedIcon = NULL;
	}
}

void home_window_load(Window *window)
{
	home_build_ui(window);
}

// Rebuild the UI if it was freed while another window was on top, and redraw so
// the Pinned stops count is up to date after pinning/unpinning elsewhere.
void home_window_appear(Window *window)
{
	home_build_ui(window);
	if (homeMenuLayer) {
		menu_layer_reload_data(homeMenuLayer);
		// Restore the row the user left on before the menu was freed.
		menu_layer_set_selected_index(homeMenuLayer, MenuIndex(0, savedSelectedRow), MenuRowAlignCenter, false);
	}
}

// Free the UI whenever another window covers the home menu, returning its memory
// to the heap for the window on top.
void home_window_disappear(Window *window)
{
	home_destroy_ui();
}

void home_window_unload(Window *window)
{
	home_destroy_ui();
}

void home_window_create()
{
	homeWindow = window_create();
	window_set_window_handlers(homeWindow, (WindowHandlers) {
		.load = home_window_load,
		.appear = home_window_appear,
		.disappear = home_window_disappear,
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
