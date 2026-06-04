#include <pebble.h>
#include "s_loading_window.h"
#include "main_window.h"
#include "error_window.h"

static Window *loadingWindow;
static GBitmap *loadingImage;
static BitmapLayer *loadingImageLayer;
static TextLayer *loadingText;

static bool stop_transfer_started;
int stop_index;
struct StopInfo stops[NUM_STOPS];

void loading_process_tuple(Tuple *t) 
{
	uint32_t key = t->key;

	if (key == MESSAGE_KEY_stopCode) {
		char* value = t->value->cstring;
		strcpy(stops[stop_index].code, value);
		//APP_LOG(APP_LOG_LEVEL_INFO, "Received key %d with value %s", key, value);
	}
	else if (key == MESSAGE_KEY_stopName) {
		char* value = t->value->cstring;
		strcpy(stops[stop_index].name, value);
		//APP_LOG(APP_LOG_LEVEL_INFO, "Received key %d with value %s", key, value);
	}
	else if (key == MESSAGE_KEY_stopDist) {
		int value = t->value->int32;
		stops[stop_index].dist = value;
		//APP_LOG(APP_LOG_LEVEL_INFO, "Received key %d with value %d", key, value);
		// End of the message read, increment the stop index
		++stop_index;
	}
	else {
		APP_LOG(APP_LOG_LEVEL_ERROR, "S_LoadingWindow: AppMessage contained obscure key!");
	}
}

void loading_message_inbox(DictionaryIterator *iter, void *context)
{
	Tuple *t = dict_read_first(iter);
	if (t) {
		if (t->key == MESSAGE_KEY_stopMessage) {
			if (!stop_transfer_started) {
				APP_LOG(APP_LOG_LEVEL_INFO, "LoadingWindow: Started stop message transfer.");
				stop_transfer_started = true;
				stop_index = 0;
			}
		}
		else if (t->key == MESSAGE_KEY_messageEnd) {
			APP_LOG(APP_LOG_LEVEL_INFO, "LoadingWindow: Stop message transfer ended. Total of %d stops were transfered.", stop_index);
			stop_transfer_started = false;
			main_window_update_stops(stops, stop_index);
			window_stack_remove(loadingWindow, true);
			window_stack_push(main_window_get_window(), true);
			vibes_short_pulse();
		}
		else if (t->key == MESSAGE_KEY_stopNoFound) {
			APP_LOG(APP_LOG_LEVEL_WARNING, "JS component was not able to find stops!");
			error_window_set_error("Was not able to find nearby stops.");
			error_window_show();
		}
		else if (t->key == MESSAGE_KEY_noGps) {
			APP_LOG(APP_LOG_LEVEL_ERROR, "JS reported that location was not found!");
			error_window_set_error("Was not able to determine your location.");
			error_window_show();
		}
		else {
			// Ignore the rest of the messages
			return;
		}
	}
	while (t != NULL) {
		t = dict_read_next(iter);
		if (t) {
			loading_process_tuple(t);
		}
	}
}

void loading_message_inbox_dropped(AppMessageResult reason, void *context)
{
	APP_LOG(APP_LOG_LEVEL_INFO, "Message dropped, reason: %d", reason);
}

void s_setup_loading_layer(Window *window)
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
	text_layer_set_text(loadingText, "Finding nearest stops...");
	text_layer_set_font(loadingText, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
	text_layer_set_text_color(loadingText, GColorWhite);
	text_layer_set_text_alignment(loadingText, GTextAlignmentCenter);
	layer_add_child(window_layer, text_layer_get_layer(loadingText));
}

void s_loading_window_load(Window *window) 
{
	s_setup_loading_layer(window);
	
	app_message_register_inbox_received(loading_message_inbox);
	app_message_register_inbox_dropped(loading_message_inbox_dropped);
	// TODO: Check these sizes
	app_message_open(MAX_INBOX_SIZE, MAX_OUTBOX_SIZE);
	
	stop_transfer_started = false;
	
	// TODO: Extract these to function
	DictionaryIterator *iter;
	app_message_outbox_begin(&iter);
	
	if (iter == NULL) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "DictionaryIterator NULL!");
		return;
	}
	
	dict_write_uint16(iter, MESSAGE_KEY_stopMessage, 1);
	dict_write_end(iter);
	
	app_message_outbox_send();
}
	
void s_loading_window_unload(Window *window) 
{
	app_message_deregister_callbacks();
		
	gbitmap_destroy(loadingImage);
	bitmap_layer_destroy(loadingImageLayer);
	text_layer_destroy(loadingText);
}

void s_loading_window_create() 
{
	loadingWindow = window_create();
	window_set_window_handlers(loadingWindow, (WindowHandlers) {
		.load = s_loading_window_load,
		.unload = s_loading_window_unload
	});
}

void s_loading_window_destroy() 
{
	if (loadingWindow != NULL) {
		window_destroy(loadingWindow);
		loadingWindow = NULL;
	}
}

Window *s_loading_window_get_window() 
{
	return loadingWindow;	
}

