#include <pebble.h>
#include "splash_window.h"

static Window *splashWindow;
static GBitmap *splashImage;
static BitmapLayer *splashImageLayer;
static TextLayer *splashImageTitle;

void splash_window_load(Window *window) 
{
	Layer *window_layer = window_get_root_layer(window);
	#if PBL_ROUND
	  GRect imagePos = GRect(20, 0, 144, 140);
	  GRect titlePos = GRect(20, 93, 144, 50);
	#elif PBL_PLATFORM_EMERY
	  GRect imagePos = GRect(30, 25, 144, 140);
	  GRect titlePos = GRect(30, 120, 144, 50);
	#else
	  GRect imagePos = GRect(0, 0, 144, 140);
	  GRect titlePos = GRect(0, 93, 144, 50);
	#endif
	
 	window_set_background_color(window, COLOR_FALLBACK(BG_COLOR, GColorBlack));
	
	splashImage = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_SPLASH);
	//splashImageLayer = bitmap_layer_create(PBL_IF_RECT_ELSE(GRect(0, 0, 144, 140), GRect(20, 0, 144, 140)));
	splashImageLayer = bitmap_layer_create(imagePos);
	
	bitmap_layer_set_bitmap(splashImageLayer, splashImage);
	bitmap_layer_set_compositing_mode(splashImageLayer, GCompOpSet);
	// Get actual Layer from the BitmapLayer and add it to the layer of the window
	layer_add_child(window_layer, bitmap_layer_get_layer(splashImageLayer));

	//splashImageTitle = text_layer_create(PBL_IF_RECT_ELSE(GRect(0, 93, 144, 50), GRect(20, 93, 144, 50)));
	splashImageTitle = text_layer_create(titlePos);
  text_layer_set_background_color(splashImageTitle, COLOR_FALLBACK(BG_COLOR, GColorBlack));
	text_layer_set_text(splashImageTitle, "Trebble");
	text_layer_set_font(splashImageTitle, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
	text_layer_set_text_color(splashImageTitle, GColorWhite);
	text_layer_set_text_alignment(splashImageTitle, GTextAlignmentCenter);
	layer_add_child(window_layer, text_layer_get_layer(splashImageTitle));
}
	
void splash_window_unload(Window *window) 
{
	gbitmap_destroy(splashImage);
	bitmap_layer_destroy(splashImageLayer);
	text_layer_destroy(splashImageTitle);
}

void splash_window_create() 
{
	splashWindow = window_create();
	window_set_window_handlers(splashWindow, (WindowHandlers) {
		.load = splash_window_load,
		.unload = splash_window_unload
	});
}

void splash_window_destroy() 
{
	window_destroy(splashWindow);
}

Window *splash_window_get_window() 
{
	return splashWindow;	
}

