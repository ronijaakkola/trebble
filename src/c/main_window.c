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

void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) 
{
	char str[10];
  switch (cell_index->section) {
		case 0:
			snprintf(str, 12, "%d meters", stops[cell_index->row].dist);
			menu_cell_basic_draw(ctx, cell_layer, stops[cell_index->row].name, str, NULL);
			break;
	}
}

void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) 
{
	// TODO: Maybe check that the cell_index is within the stop amount?
	l_loading_window_show(stops[cell_index->row].code, stops[cell_index->row].name);
}

void setup_menu_layer(Window *window, Layer *window_layer) 
{
	GRect window_bounds = layer_get_bounds(window_layer);
	window_bounds.origin.y = STATUS_BAR_LAYER_HEIGHT;
	
	mainMenuLayer = menu_layer_create(window_bounds);
	menu_layer_set_callbacks(mainMenuLayer, NULL, (MenuLayerCallbacks){
		#ifdef PBL_RECT
		 .get_num_sections = menu_get_num_sections_callback,
		 .get_header_height = menu_get_header_height_callback,
		 .draw_header = menu_draw_header_callback,
		#endif
		.get_num_rows = menu_get_num_rows_callback,
		.draw_row = menu_draw_row_callback,
		.select_click = menu_select_callback,
	});

	menu_layer_set_highlight_colors(mainMenuLayer, COLOR_FALLBACK(MENU_HL_COLOR, GColorBlack), GColorWhite);
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