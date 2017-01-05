#include <pebble.h>
#include "l_loading_window.h"
#include "lines_window.h"
#include "error_window.h"

static Window *l_loadingWindow;
static GBitmap *loadingImage;
static BitmapLayer *loadingImageLayer;
static TextLayer *loadingText;

char stopCode[5] = {0};
char stopName[30] = {0};
static bool line_transfer_started;
static bool line_got_name;
static bool line_got_time;
static bool line_got_dir;
int line_index;
struct LineInfo lines[NUM_LINES];

void l_loading_window_show(char *code, char *name)
{
	strncpy(stopCode, code, 4);
	strncpy(stopName, name, 30);
	window_stack_push(l_loading_window_get_window(), true);
}

void l_loading_process_tuple(Tuple *t) 
{
	int key = t->key;

	if (key == 10007) {
		char* value = t->value->cstring;
		//strcpy(lines[line_index].code, value);
		strncpy(lines[line_index].code, value, 10);
		line_got_name = true;
	}
	else if (key == 10008) {
		char* value = t->value->cstring;
		//strcpy(lines[line_index].time, value);
		strncpy(lines[line_index].time, value, 10);
		line_got_time = true;
	}
	else if (key == 10011) {
		char* value = t->value->cstring;
		//strcpy(lines[line_index].dir, value);
		strncpy(lines[line_index].dir, value, 30);
		line_got_dir = true;
	}
	else if (key == 10006) {
		return;
	}
	else {
		APP_LOG(APP_LOG_LEVEL_ERROR, "L_LoadingWindow: AppMessage contained obscure key!");
	}
}

void l_loading_message_inbox(DictionaryIterator *iter, void *context)
{
	// TODO: NOTE: Seems that we cannot trust that the keys are in the order we specified.
	// This is a quick hack to dig the message key from the dictionary but we probably
	// should do this smarter some day.
	Tuple *t = dict_find(iter, 10006);
	if (t) {
		if (!line_transfer_started) {
			APP_LOG(APP_LOG_LEVEL_INFO, "LoadingWindow: Started lines message transfer.");
			line_transfer_started = true;
			line_got_name = false;
			line_got_time = false;
			line_got_dir = false;
			line_index = 0;
		}
	}
	else {
		t = dict_find(iter, 10004);	
		if (t) {
			APP_LOG(APP_LOG_LEVEL_INFO, "LoadingWindow: Line message transfer ended. Total of %d lines were transfered.", line_index);
			line_transfer_started = false;
			lines_window_update_lines(lines, line_index, stopName);
			window_stack_remove(l_loadingWindow, true);
			window_stack_push(lines_window_get_window(), true);
			vibes_short_pulse();
			return;
		}
	}
	
	t = dict_read_first(iter);
	if (t) {
		if (t->key == 10009) {
			APP_LOG(APP_LOG_LEVEL_WARNING, "JS component was not able to find departing lines!");
			window_stack_remove(l_loadingWindow, true);
			error_window_set_error("Was not able to find lines departing soon.");
			error_window_show();
			return;
		}
		l_loading_process_tuple(t);
	}
	while (t != NULL) {
		t = dict_read_next(iter);
		if (t) {
			l_loading_process_tuple(t);
		}
	}
	
	if (line_got_name && line_got_time && line_got_dir) {
		++line_index;
		line_got_name = false;
		line_got_time = false;
		line_got_dir = false;
	}
}

void l_loading_message_inbox_dropped(AppMessageResult reason, void *context)
{
	APP_LOG(APP_LOG_LEVEL_INFO, "Message dropped, reason: %d", reason);
}

void l_setup_loading_layer(Window *window)
{
	Layer *window_layer = window_get_root_layer(window);
	
	#if PBL_ROUND
	  GRect imagePos = GRect(20, 0, 144, 140);
	  GRect textPos = GRect(20, 100, 144, 50);
	#elif PBL_PLATFORM_EMERY
	  GRect imagePos = GRect(30, 25, 144, 140);
	  GRect textPos = GRect(30, 125, 144, 50);
	#else
	  GRect imagePos = GRect(0, 0, 144, 140);
	  GRect textPos = GRect(0, 100, 144, 50);
	#endif
	
  window_set_background_color(window, COLOR_FALLBACK(LOADING_BG_COLOR, GColorBlack));
	
	loadingImage = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BUS_STOP);
	//loadingImageLayer = bitmap_layer_create(PBL_IF_RECT_ELSE(GRect(0, 0, 144, 140), GRect(20, 0, 144, 140)));
	loadingImageLayer = bitmap_layer_create(imagePos);
	bitmap_layer_set_bitmap(loadingImageLayer, loadingImage);
	bitmap_layer_set_compositing_mode(loadingImageLayer, GCompOpSet);
	// Get actual Layer from the BitmapLayer and add it to the layer of the window
	layer_add_child(window_layer, bitmap_layer_get_layer(loadingImageLayer));
	
	//loadingText = text_layer_create(PBL_IF_RECT_ELSE(GRect(0, 100, 144, 50), GRect(20, 100, 144, 50)));
	loadingText = text_layer_create(textPos);
  text_layer_set_background_color(loadingText, COLOR_FALLBACK(LOADING_BG_COLOR, GColorBlack));
	text_layer_set_text(loadingText, "Finding next departing busses...");
	text_layer_set_font(loadingText, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
	text_layer_set_text_color(loadingText, GColorWhite);
	text_layer_set_text_alignment(loadingText, GTextAlignmentCenter);
	layer_add_child(window_layer, text_layer_get_layer(loadingText));
}

void l_loading_window_load(Window *window) 
{
	l_setup_loading_layer(window);
	
	app_message_register_inbox_received(l_loading_message_inbox);
	app_message_register_inbox_dropped(l_loading_message_inbox_dropped);
	
	line_transfer_started = false;
	
	// TODO: Extract these to function
	DictionaryIterator *iter;
	app_message_outbox_begin(&iter);
	
	if (iter == NULL) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "DictionaryIterator NULL!");
		return;
	}
	
	dict_write_cstring(iter, 10006, (char *)stopCode);
	dict_write_end(iter);
	
	app_message_outbox_send();
}
	
void l_loading_window_unload(Window *window) 
{
	app_message_deregister_callbacks();
	
	gbitmap_destroy(loadingImage);
	bitmap_layer_destroy(loadingImageLayer);
	text_layer_destroy(loadingText);
}

void l_loading_window_create() 
{
	l_loadingWindow = window_create();
	window_set_window_handlers(l_loadingWindow, (WindowHandlers) {
		.load = l_loading_window_load,
		.unload = l_loading_window_unload
	});
}

void l_loading_window_destroy() 
{
	window_destroy(l_loadingWindow);
}

Window *l_loading_window_get_window() 
{
	return l_loadingWindow;	
}
