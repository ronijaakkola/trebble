#include <pebble.h>
#include "feedback_window.h"

// How long the confirmation stays up before auto-dismissing.
#define FEEDBACK_DURATION_MS 1500

static Window *feedbackWindow;
static TextLayer *messageLayer;
static char message[64];
static AppTimer *dismissTimer;

// Pops the window (animated, so it slides away to the right) and cancels the
// auto-dismiss timer if it is still pending.
static void feedback_dismiss(void)
{
	if (dismissTimer) {
		app_timer_cancel(dismissTimer);
		dismissTimer = NULL;
	}
	window_stack_remove(feedbackWindow, true);
}

static void feedback_timer_callback(void *data)
{
	dismissTimer = NULL;
	window_stack_remove(feedbackWindow, true);
}

static void feedback_click_handler(ClickRecognizerRef recognizer, void *context)
{
	feedback_dismiss();
}

static void feedback_click_config_provider(void *context)
{
	// Any button dismisses the confirmation immediately.
	window_single_click_subscribe(BUTTON_ID_UP, feedback_click_handler);
	window_single_click_subscribe(BUTTON_ID_SELECT, feedback_click_handler);
	window_single_click_subscribe(BUTTON_ID_DOWN, feedback_click_handler);
	window_single_click_subscribe(BUTTON_ID_BACK, feedback_click_handler);
}

void feedback_window_load(Window *window)
{
	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);

	window_set_background_color(window, COLOR_FALLBACK(GColorCobaltBlue, GColorBlack));

	// Center the message both horizontally (via text alignment) and vertically.
	// TextLayer only does horizontal alignment, so the wrapped text's height is
	// measured and the frame is positioned to sit in the vertical middle.
	int16_t margin = PBL_IF_ROUND_ELSE(20, 8);
	int16_t avail_w = bounds.size.w - 2 * margin;
	GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);

	GSize content = graphics_text_layout_get_content_size(
		message, font, GRect(0, 0, avail_w, bounds.size.h),
		GTextOverflowModeWordWrap, GTextAlignmentCenter);

	GRect text_frame = GRect(margin, (bounds.size.h - content.h) / 2, avail_w, content.h);

	messageLayer = text_layer_create(text_frame);
	text_layer_set_background_color(messageLayer, GColorClear);
	text_layer_set_text_color(messageLayer, GColorWhite);
	text_layer_set_font(messageLayer, font);
	text_layer_set_text_alignment(messageLayer, GTextAlignmentCenter);
	text_layer_set_overflow_mode(messageLayer, GTextOverflowModeWordWrap);
	text_layer_set_text(messageLayer, message);
	layer_add_child(window_layer, text_layer_get_layer(messageLayer));
}

void feedback_window_unload(Window *window)
{
	if (dismissTimer) {
		app_timer_cancel(dismissTimer);
		dismissTimer = NULL;
	}
	text_layer_destroy(messageLayer);
}

void feedback_window_appear(Window *window)
{
	// Start the auto-dismiss countdown once the window is actually on screen.
	dismissTimer = app_timer_register(FEEDBACK_DURATION_MS, feedback_timer_callback, NULL);
}

void feedback_window_create(void)
{
	feedbackWindow = window_create();
	window_set_click_config_provider(feedbackWindow, feedback_click_config_provider);
	window_set_window_handlers(feedbackWindow, (WindowHandlers) {
		.load = feedback_window_load,
		.appear = feedback_window_appear,
		.unload = feedback_window_unload
	});
}

void feedback_window_destroy(void)
{
	window_destroy(feedbackWindow);
}

void feedback_window_show(const char *message_text)
{
	strncpy(message, message_text, sizeof(message) - 1);
	message[sizeof(message) - 1] = '\0';
	window_stack_push(feedbackWindow, true);
}
