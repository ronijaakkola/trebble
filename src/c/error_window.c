#include <pebble.h>
#include "error_window.h"

static Window *errorWindow;
static GBitmap *errorImage;
static BitmapLayer *errorImageLayer;
static char currentErrorMessage[1][50];
static TextLayer *errorMessageLayer;

// Set new error message
// NOTE: This has to be called before pushing the window to the stack
void error_window_set_error(char *errorMessage) 
{
	strncpy(currentErrorMessage[0], errorMessage, sizeof(currentErrorMessage[0]));
}

void error_window_show()
{
	window_stack_push(errorWindow, true);
}

TextLayer *error_new_text_layer() 
{
	#if PBL_ROUND
	  GRect textPos = GRect(20, 110, 144, 50);
	#elif PBL_PLATFORM_EMERY
	  GRect textPos = GRect(30, 125, 144, 50);
	#else
	  GRect textPos = GRect(0, 110, 144, 50);
	#endif
	
	TextLayer *newTextLayer = text_layer_create(textPos);
	#ifdef PBL_COLOR
  	text_layer_set_background_color(newTextLayer, GColorDarkGray);
	#else
  	text_layer_set_background_color(newTextLayer, GColorBlack);
	#endif
	text_layer_set_font(newTextLayer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
	text_layer_set_text_color(newTextLayer, GColorWhite);
	text_layer_set_text_alignment(newTextLayer, GTextAlignmentCenter);
	return newTextLayer;
}

void error_window_load(Window *window) 
{
	Layer *window_layer = window_get_root_layer(window);
	
	#if PBL_ROUND
	  GRect imagePos = GRect(20, 0, 144, 140);
	#elif PBL_PLATFORM_EMERY
	  GRect imagePos = GRect(30, 20, 144, 140);
	#else
	  GRect imagePos = GRect(0, 0, 144, 140);
	#endif
	
	#ifdef PBL_COLOR
  	window_set_background_color(window, GColorDarkGray);
	#else
  	window_set_background_color(window, GColorBlack);
	#endif
	
	errorImage = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ERROR);
	errorImageLayer = bitmap_layer_create(imagePos);
	bitmap_layer_set_bitmap(errorImageLayer, errorImage);
	bitmap_layer_set_compositing_mode(errorImageLayer, GCompOpSet);
	
	// Get actual Layer from the BitmapLayer and add it to the layer of the window
	layer_add_child(window_layer, bitmap_layer_get_layer(errorImageLayer));
	
	errorMessageLayer = error_new_text_layer();
	text_layer_set_text(errorMessageLayer, (char *)currentErrorMessage);
	layer_add_child(window_layer, text_layer_get_layer(errorMessageLayer));
}
	
void error_window_unload(Window *window) 
{
	gbitmap_destroy(errorImage);
	bitmap_layer_destroy(errorImageLayer);
	text_layer_destroy(errorMessageLayer);
	errorMessageLayer = NULL;
}

void error_window_create() 
{
	errorWindow = window_create();
	window_set_window_handlers(errorWindow, (WindowHandlers) {
		.load = error_window_load,
		.unload = error_window_unload
	});
}

void error_window_destroy() 
{
	window_destroy(errorWindow);
}

Window *error_window_get_window() 
{
	return errorWindow;	
}
