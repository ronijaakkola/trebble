#include <pebble.h>
#include "main_window.h"
#include "s_loading_window.h"
#include "l_loading_window.h"
#include "error_window.h"

static Window *mainWindow;
static MenuLayer *mainMenuLayer;
static StatusBarLayer *statusLayer;

static int stopAmount = 0;
static struct StopInfo stops[NUM_STOPS];

void main_window_update_stops(struct StopInfo newStops[NUM_STOPS], int amount)
{
	// Update stop array
	for(int i = 0; i < NUM_STOPS; ++i) {
		stops[i] = newStops[i];
	}
	stopAmount = amount;
}

uint16_t menu_get_num_sections_callback(MenuLayer *menu_layer, void *data)
{
	return 1;
}

uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
	switch (section_index) {
		case 0:
			return stopAmount;
		default:
			return 0;
	}
}

int16_t menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
	return MENU_CELL_BASIC_HEADER_HEIGHT;
}

void menu_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data)
{
	switch (section_index) {
		case 0:
			menu_cell_basic_header_draw(ctx, cell_layer, "Nearest stops");
			break;
	}
}

int16_t menu_get_cell_height_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	return 48; // A bit taller than the Pencil design's 46 for breathing room
}

void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data)
{
	if (cell_index->section != 0) {
		return;
	}

	struct StopInfo *stop = &stops[cell_index->row];
	GRect bounds = layer_get_bounds(cell_layer);

	#ifdef PBL_COLOR
		GColor text_color = GColorBlack;
	#else
		GColor text_color = menu_cell_layer_is_highlighted(cell_layer) ? GColorWhite : GColorBlack;
	#endif

	// Colored type dot on the left: bus is blue, tram red. On black-and-white
	// watches both fall back to black. Unknown types get no dot.
	int16_t cy = bounds.size.h / 2;
	if (stop->type[0] == 'B' || stop->type[0] == 'T') {
		GColor circle_color = stop->type[0] == 'B'
			? COLOR_FALLBACK(GColorCobaltBlue, GColorBlack)
			: COLOR_FALLBACK(GColorRed, GColorBlack);
		graphics_context_set_fill_color(ctx, circle_color);
		graphics_fill_circle(ctx, GPoint(16, cy-4), 12);
	}

	int16_t text_x = 34;
	int16_t text_w = bounds.size.w - text_x - 4;

	graphics_context_set_text_color(ctx, text_color);
	graphics_draw_text(ctx, stop->name, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(text_x, cy - 21, text_w, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

	char dist[12];
	snprintf(dist, sizeof(dist), "%d m", stop->dist);
	graphics_draw_text(ctx, dist, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(text_x, cy, text_w, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	l_loading_window_show(stops[cell_index->row].code, stops[cell_index->row].name);
}

void setup_menu_layer(Window *window, Layer *window_layer)
{
	GRect window_bounds = layer_get_bounds(window_layer);
	window_bounds.origin.y = STATUS_BAR_LAYER_HEIGHT;

	mainMenuLayer = menu_layer_create(window_bounds);
	// This has to be done slightly differently for Pebble Round
	menu_layer_set_callbacks(mainMenuLayer, NULL, (MenuLayerCallbacks){
		#ifdef PBL_RECT
		 .get_num_sections = menu_get_num_sections_callback,
		 .get_header_height = menu_get_header_height_callback,
		 .draw_header = menu_draw_header_callback,
		#endif
		.get_num_rows = menu_get_num_rows_callback,
		.get_cell_height = menu_get_cell_height_callback,
		.draw_row = menu_draw_row_callback,
		.select_click = menu_select_callback,
	});

	menu_layer_set_highlight_colors(mainMenuLayer, COLOR_FALLBACK(MENU_HL_COLOR, GColorBlack), COLOR_FALLBACK(GColorBlack, GColorWhite));
	menu_layer_set_click_config_onto_window(mainMenuLayer, window);

	layer_add_child(window_layer, menu_layer_get_layer(mainMenuLayer));
}

void main_window_load(Window *window)
{
	Layer *window_layer = window_get_root_layer(window);

	setup_menu_layer(window, window_layer);

	statusLayer = status_bar_layer_create();
  status_bar_layer_set_separator_mode(statusLayer, StatusBarLayerSeparatorModeDotted);
  status_bar_layer_set_colors(statusLayer, GColorClear, GColorBlack);
	layer_add_child(window_layer, status_bar_layer_get_layer(statusLayer));

	s_loading_window_destroy();
}

void main_window_unload(Window *window)
{
	menu_layer_destroy(mainMenuLayer);
	status_bar_layer_destroy(statusLayer);
}

void main_window_create()
{
	mainWindow = window_create();
	window_set_window_handlers(mainWindow, (WindowHandlers) {
		.load = main_window_load,
		.unload = main_window_unload
	});
}

void main_window_destroy()
{
	window_destroy(mainWindow);
}

Window *main_window_get_window()
{
	return mainWindow;
}
