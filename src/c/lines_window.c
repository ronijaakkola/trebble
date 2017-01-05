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
		return PBL_IF_RECT_ELSE(47, 67);
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

void lines_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) 
{
  switch (cell_index->section) {
		case 0:
			if (menu_cell_layer_is_highlighted(cell_layer)) {
				graphics_context_set_text_color(ctx, GColorWhite);
			} 
			else {
				graphics_context_set_text_color(ctx, GColorBlack);
			}
		
			#ifdef PBL_ROUND
				graphics_draw_text(ctx, lines[cell_index->row].code, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GRect(0, 0, 180, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
				graphics_draw_text(ctx, lines[cell_index->row].dir, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(0, 24, 180, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
			 	graphics_draw_text(ctx, lines[cell_index->row].time, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(0, 42, 180, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
		  #elif PBL_PLATFORM_EMERY
		    menu_cell_title_draw(ctx, cell_layer, lines[cell_index->row].code);
				graphics_draw_text(ctx, lines[cell_index->row].dir, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(3, 23, 140, 88), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
			 	graphics_draw_text(ctx, lines[cell_index->row].time, fonts_get_system_font(FONT_KEY_GOTHIC_24), GRect(135, 0, 62, 20), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
			#else
				menu_cell_title_draw(ctx, cell_layer, lines[cell_index->row].code);
				graphics_draw_text(ctx, lines[cell_index->row].dir, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(3, 23, 140, 88), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
			 	graphics_draw_text(ctx, lines[cell_index->row].time, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(80, 3, 62, 20), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
			#endif
		
			break;
	}
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

	menu_layer_set_highlight_colors(lineMenuLayer, COLOR_FALLBACK(MENU_HL_COLOR, GColorBlack), GColorWhite);
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