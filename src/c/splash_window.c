#include <pebble.h>
#include "splash_window.h"

static Window *splashWindow;
static GDrawCommandImage *splashIcon;
static Layer *splashIconLayer;
static TextLayer *splashImageTitle;

// Recolor every command in the PDC so the bus logo renders with black strokes
// and a white fill, standing out cleanly on the cobalt-blue splash background.
static void pdc_set_colors(GDrawCommandImage *image, GColor stroke, GColor fill)
{
	GDrawCommandList *list = gdraw_command_image_get_command_list(image);
	uint32_t num = gdraw_command_list_get_num_commands(list);
	for (uint32_t i = 0; i < num; i++) {
		GDrawCommand *cmd = gdraw_command_list_get_command(list, i);
		gdraw_command_set_stroke_color(cmd, stroke);
		gdraw_command_set_fill_color(cmd, fill);
	}
}

// Draws the 50x50 bus PDC icon centered within its layer.
static void splash_icon_update_proc(Layer *layer, GContext *ctx)
{
	if (!splashIcon) {
		return;
	}
	GRect bounds = layer_get_bounds(layer);
	GSize size = gdraw_command_image_get_bounds_size(splashIcon);
	GPoint origin = GPoint((bounds.size.w - size.w) / 2, (bounds.size.h - size.h) / 2);
	gdraw_command_image_draw(ctx, splashIcon, origin);
}

void splash_window_load(Window *window)
{
	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);

	// Vertically center an icon + logotype block: the 50px bus icon sits above the
	// "Vuoro" wordmark. The icon layer is full-width so the PDC self-centers
	// horizontally; the title uses the Droid Serif bold logotype font.
	const int iconBand = 50;
	const int titleBand = 34;
	const int gap = 6;
	int blockTop = (bounds.size.h - (iconBand + gap + titleBand)) / 2;
	GRect iconPos  = GRect(0, blockTop, bounds.size.w, iconBand);
	GRect titlePos = GRect(0, blockTop + iconBand + gap, bounds.size.w, titleBand);

 	window_set_background_color(window, COLOR_FALLBACK(BG_COLOR, GColorBlack));

	splashIcon = gdraw_command_image_create_with_resource(RESOURCE_ID_IMAGE_SPLASH_BUS);
	pdc_set_colors(splashIcon, GColorBlack, GColorWhite);
	splashIconLayer = layer_create(iconPos);
	layer_set_update_proc(splashIconLayer, splash_icon_update_proc);
	layer_add_child(window_layer, splashIconLayer);

	splashImageTitle = text_layer_create(titlePos);
  text_layer_set_background_color(splashImageTitle, GColorClear);
	text_layer_set_text(splashImageTitle, "Vuoro");
	text_layer_set_font(splashImageTitle, fonts_get_system_font(FONT_KEY_DROID_SERIF_28_BOLD));
	text_layer_set_text_color(splashImageTitle, GColorWhite);
	text_layer_set_text_alignment(splashImageTitle, GTextAlignmentCenter);
	layer_add_child(window_layer, text_layer_get_layer(splashImageTitle));
}

void splash_window_unload(Window *window)
{
	if (splashIcon) {
		gdraw_command_image_destroy(splashIcon);
		splashIcon = NULL;
	}
	if (splashIconLayer) {
		layer_destroy(splashIconLayer);
		splashIconLayer = NULL;
	}
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

