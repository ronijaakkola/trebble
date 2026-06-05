#include <pebble.h>
#include "error_window.h"

static Window *errorWindow;
static char currentErrorMessage[50];
static ErrorIcon currentErrorIcon = ERROR_ICON_GENERIC;

// Generic error uses the legacy raster icon; the connection errors use crisp
// PDC vector icons. Only one of the two is created per show, based on the icon.
static GBitmap *errorImage;
static BitmapLayer *errorImageLayer;
static GDrawCommandImage *errorPdc;
static Layer *errorIconLayer;

static TextLayer *errorMessageLayer;

// Set new error message and icon
// NOTE: This has to be called before pushing the window to the stack
void error_window_set_error(const char *errorMessage, ErrorIcon icon)
{
	strncpy(currentErrorMessage, errorMessage, sizeof(currentErrorMessage) - 1);
	currentErrorMessage[sizeof(currentErrorMessage) - 1] = '\0';
	currentErrorIcon = icon;
}

void error_window_show()
{
	window_stack_push(errorWindow, true);
}

static uint32_t pdc_resource_for_icon(ErrorIcon icon)
{
	switch (icon) {
		case ERROR_ICON_NO_PHONE:
			return RESOURCE_ID_IMAGE_NO_PHONE_CONNECTION;
		case ERROR_ICON_NO_INTERNET:
			return RESOURCE_ID_IMAGE_NO_INTERNET_CONNECTION;
		default:
			return 0;
	}
}

// Draws the PDC icon centered within its layer.
static void error_icon_update_proc(Layer *layer, GContext *ctx)
{
	if (!errorPdc) {
		return;
	}
	GRect bounds = layer_get_bounds(layer);
	GSize size = gdraw_command_image_get_bounds_size(errorPdc);
	GPoint origin = GPoint((bounds.size.w - size.w) / 2, (bounds.size.h - size.h) / 2);
	gdraw_command_image_draw(ctx, errorPdc, origin);
}

TextLayer *error_new_text_layer(GRect textPos)
{
	TextLayer *newTextLayer = text_layer_create(textPos);
	text_layer_set_background_color(newTextLayer, GColorClear);
	text_layer_set_font(newTextLayer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
	text_layer_set_text_color(newTextLayer, GColorBlack);
	text_layer_set_text_alignment(newTextLayer, GTextAlignmentCenter);
	return newTextLayer;
}

void error_window_load(Window *window)
{
	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);

	// Gray error screen with the icon centered above a wrapping message, matching
	// the standard Pebble no-connection layout.
	#if PBL_ROUND
	  GRect iconRect = GRect(0, 8, bounds.size.w, 104);
	  GRect textPos  = GRect(12, 112, bounds.size.w - 24, 64);
	#elif PBL_PLATFORM_EMERY
	  GRect iconRect = GRect(0, 18, bounds.size.w, 124);
	  GRect textPos  = GRect(6, 146, bounds.size.w - 12, 72);
	#else
	  GRect iconRect = GRect(0, 6, bounds.size.w, 96);
	  GRect textPos  = GRect(4, 100, bounds.size.w - 8, 64);
	#endif

	window_set_background_color(window, COLOR_FALLBACK(GColorLightGray, GColorWhite));

	if (currentErrorIcon == ERROR_ICON_GENERIC) {
		errorImage = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ERROR);
		errorImageLayer = bitmap_layer_create(iconRect);
		bitmap_layer_set_bitmap(errorImageLayer, errorImage);
		bitmap_layer_set_compositing_mode(errorImageLayer, GCompOpSet);
		layer_add_child(window_layer, bitmap_layer_get_layer(errorImageLayer));
	} else {
		errorPdc = gdraw_command_image_create_with_resource(pdc_resource_for_icon(currentErrorIcon));
		errorIconLayer = layer_create(iconRect);
		layer_set_update_proc(errorIconLayer, error_icon_update_proc);
		layer_add_child(window_layer, errorIconLayer);
	}

	errorMessageLayer = error_new_text_layer(textPos);
	text_layer_set_text(errorMessageLayer, currentErrorMessage);
	layer_add_child(window_layer, text_layer_get_layer(errorMessageLayer));
}

void error_window_unload(Window *window)
{
	if (errorImageLayer) {
		bitmap_layer_destroy(errorImageLayer);
		errorImageLayer = NULL;
	}
	if (errorImage) {
		gbitmap_destroy(errorImage);
		errorImage = NULL;
	}
	if (errorIconLayer) {
		layer_destroy(errorIconLayer);
		errorIconLayer = NULL;
	}
	if (errorPdc) {
		gdraw_command_image_destroy(errorPdc);
		errorPdc = NULL;
	}
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
