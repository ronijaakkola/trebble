#include <pebble.h>
#include "pins_window.h"
#include "pins.h"
#include "main_window.h"   // struct StopInfo, NUM_STOPS
#include "lines_window.h"
#include "error_window.h"

static Window *pinsWindow;
static MenuLayer *pinsMenuLayer;
static StatusBarLayer *statusLayer;
static TextLayer *loadingLayer;
static TextLayer *titleLayer;

static int pinAmount = 0;
static struct StopInfo pinStops[NUM_STOPS];
static bool pin_transfer_started;
static int pin_index;

// Shows the title + centered status text (loading / empty / error), or hides
// them to reveal the populated pinned stops menu (whose own header takes over).
static void pins_set_loading(bool loading)
{
	// The overlay layers only exist while the window is built (see pins_build_ui).
	if (!loadingLayer || !titleLayer) {
		return;
	}
	layer_set_hidden(text_layer_get_layer(loadingLayer), !loading);
	layer_set_hidden(text_layer_get_layer(titleLayer), !loading);
}

// Maps a Digitransit vehicle mode string to a single-letter type indicator.
static void mode_to_type_letter(const char *mode, char *out)
{
	if (strcmp(mode, "BUS") == 0) {
		out[0] = 'B';
		out[1] = '\0';
	}
	else if (strcmp(mode, "TRAM") == 0) {
		out[0] = 'T';
		out[1] = '\0';
	}
	else {
		out[0] = '\0';
	}
}

static void process_pin_tuple(Tuple *t)
{
	uint32_t key = t->key;

	if (key == MESSAGE_KEY_pinCode) {
		strncpy(pinStops[pin_index].code, t->value->cstring, sizeof(pinStops[pin_index].code) - 1);
		pinStops[pin_index].code[sizeof(pinStops[pin_index].code) - 1] = '\0';
	}
	else if (key == MESSAGE_KEY_pinName) {
		strncpy(pinStops[pin_index].name, t->value->cstring, sizeof(pinStops[pin_index].name) - 1);
		pinStops[pin_index].name[sizeof(pinStops[pin_index].name) - 1] = '\0';
	}
	else if (key == MESSAGE_KEY_pinDist) {
		pinStops[pin_index].dist = t->value->int32;
	}
	else if (key == MESSAGE_KEY_pinMode) {
		mode_to_type_letter(t->value->cstring, pinStops[pin_index].type);
	}
	else if (key == MESSAGE_KEY_pinMessage) {
		// Per-pin marker; index bookkeeping happens in the inbox handler.
	}
}

void pins_message_inbox(DictionaryIterator *iter, void *context)
{
	// Each pinned stop arrives as its own message tagged with pinMessage, ordered
	// nearest-first by the JS component.
	if (dict_find(iter, MESSAGE_KEY_pinMessage)) {
		if (!pin_transfer_started) {
			pin_transfer_started = true;
			pin_index = 0;
		}
		if (pin_index >= NUM_STOPS) {
			return;
		}
		// Defaults for optional fields a pinned stop may omit.
		pinStops[pin_index].type[0] = '\0';
		pinStops[pin_index].dist = -1;
		for (Tuple *t = dict_read_first(iter); t != NULL; t = dict_read_next(iter)) {
			process_pin_tuple(t);
		}
		++pin_index;
		return;
	}

	if (dict_find(iter, MESSAGE_KEY_messageEnd)) {
		pin_transfer_started = false;
		pinAmount = pin_index;
		// The list may have been freed if the window was covered before the data
		// arrived; the count is retained so it renders when the window reappears.
		if (pinsMenuLayer) {
			menu_layer_reload_data(pinsMenuLayer);
		}
		pins_set_loading(false);
		return;
	}

	if (dict_find(iter, MESSAGE_KEY_noInternet)) {
		APP_LOG(APP_LOG_LEVEL_WARNING, "JS reported no internet connection!");
		pin_transfer_started = false;
		error_window_set_error("No internet connection", ERROR_ICON_NO_INTERNET);
		error_window_show();
		return;
	}

	if (dict_find(iter, MESSAGE_KEY_pinNoFound)) {
		APP_LOG(APP_LOG_LEVEL_WARNING, "JS could not load pinned stops!");
		pin_transfer_started = false;
		if (loadingLayer) {
			text_layer_set_text(loadingLayer, "Couldn't load");
		}
		return;
	}

	if (dict_find(iter, MESSAGE_KEY_noGps)) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "JS reported that location was not found!");
		pin_transfer_started = false;
		if (loadingLayer) {
			text_layer_set_text(loadingLayer, "No location");
		}
		return;
	}
}

void pins_window_register_inbox(void)
{
	app_message_register_inbox_received(pins_message_inbox);
}

uint16_t pins_get_num_sections_callback(MenuLayer *menu_layer, void *data)
{
	return 1;
}

uint16_t pins_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
	switch (section_index) {
		case 0:
			return pinAmount;
		default:
			return 0;
	}
}

int16_t pins_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
	return MENU_CELL_BASIC_HEADER_HEIGHT;
}

void pins_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data)
{
	if (section_index != 0) {
		return;
	}
	GRect bounds = layer_get_bounds(cell_layer);
	graphics_context_set_text_color(ctx, GColorBlack);
	graphics_draw_text(ctx, "Pinned stops", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

int16_t pins_get_cell_height_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	return 40; // Matches the nearby stops list.
}

void pins_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data)
{
	if (cell_index->section != 0) {
		return;
	}

	struct StopInfo *stop = &pinStops[cell_index->row];
	GRect bounds = layer_get_bounds(cell_layer);

	#ifdef PBL_COLOR
		GColor text_color = GColorBlack;
	#else
		GColor text_color = menu_cell_layer_is_highlighted(cell_layer) ? GColorWhite : GColorBlack;
	#endif

	// Colored type dot on the left, mirroring the nearby stops list.
	int16_t cy = bounds.size.h / 2;
	if (stop->type[0] == 'B' || stop->type[0] == 'T') {
		GColor circle_color = stop->type[0] == 'B'
			? COLOR_FALLBACK(GColorCobaltBlue, GColorBlack)
			: COLOR_FALLBACK(GColorRed, GColorBlack);
		graphics_context_set_fill_color(ctx, circle_color);
		graphics_fill_circle(ctx, GPoint(16, cy), 12);
	}

	int16_t text_x = 34;
	int16_t text_w = bounds.size.w - text_x - 4;

	// The pinned stops list shows only the stop name (no distance), so it is
	// vertically centered in the cell. The stops are still ordered nearest-first
	// using the distance computed by the JS component.
	graphics_context_set_text_color(ctx, text_color);
	graphics_draw_text(ctx, stop->name, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(text_x, cy - 13, text_w, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

void pins_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	lines_window_show(pinStops[cell_index->row].code, pinStops[cell_index->row].name);
}

void pins_long_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	struct StopInfo *stop = &pinStops[cell_index->row];
	pins_show_action_menu(stop->code, stop->name, stop->type, "Pin stop", "Unpin stop");
}

void setup_pins_menu_layer(Window *window, Layer *window_layer)
{
	GRect window_bounds = layer_get_bounds(window_layer);
	window_bounds.origin.y = STATUS_BAR_LAYER_HEIGHT;

	pinsMenuLayer = menu_layer_create(window_bounds);
	menu_layer_set_callbacks(pinsMenuLayer, NULL, (MenuLayerCallbacks){
		#ifdef PBL_RECT
		 .get_num_sections = pins_get_num_sections_callback,
		 .get_header_height = pins_get_header_height_callback,
		 .draw_header = pins_draw_header_callback,
		#endif
		.get_num_rows = pins_get_num_rows_callback,
		.get_cell_height = pins_get_cell_height_callback,
		.draw_row = pins_draw_row_callback,
		.select_click = pins_select_callback,
		.select_long_click = pins_long_select_callback,
	});

	menu_layer_set_highlight_colors(pinsMenuLayer, COLOR_FALLBACK(PIN_HL_COLOR, GColorBlack), COLOR_FALLBACK(GColorBlack, GColorWhite));
	menu_layer_set_click_config_onto_window(pinsMenuLayer, window);

	layer_add_child(window_layer, menu_layer_get_layer(pinsMenuLayer));
}

// Title + centered status text, shown over the (empty) menu until pinned stops
// arrive (or to convey the empty / error state).
void setup_pins_loading_layer(Layer *window_layer)
{
	GRect bounds = layer_get_bounds(window_layer);
	int16_t top = STATUS_BAR_LAYER_HEIGHT;

	titleLayer = text_layer_create(GRect(4, top, bounds.size.w - 8, 18));
	text_layer_set_background_color(titleLayer, GColorClear);
	text_layer_set_text_color(titleLayer, GColorBlack);
	text_layer_set_font(titleLayer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
	text_layer_set_text_alignment(titleLayer, GTextAlignmentCenter);
	text_layer_set_text(titleLayer, "Pinned stops");
	layer_add_child(window_layer, text_layer_get_layer(titleLayer));

	int16_t cy = top + (bounds.size.h - top) / 2 - 12;
	loadingLayer = text_layer_create(GRect(0, cy, bounds.size.w, 24));
	text_layer_set_background_color(loadingLayer, GColorClear);
	text_layer_set_text_color(loadingLayer, GColorBlack);
	text_layer_set_font(loadingLayer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
	text_layer_set_text_alignment(loadingLayer, GTextAlignmentCenter);
	text_layer_set_text(loadingLayer, "Loading..");
	layer_add_child(window_layer, text_layer_get_layer(loadingLayer));
}

// Builds the window's layers (menu + loading overlay + status bar). Split out so
// it can run both on initial load and when the window is revealed again after
// being covered (see pins_window_appear/disappear). No-op if already built.
static void pins_build_ui(Window *window)
{
	if (pinsMenuLayer) {
		return;
	}
	Layer *window_layer = window_get_root_layer(window);

	setup_pins_menu_layer(window, window_layer);
	setup_pins_loading_layer(window_layer);

	statusLayer = status_bar_layer_create();
	status_bar_layer_set_separator_mode(statusLayer, StatusBarLayerSeparatorModeDotted);
	status_bar_layer_set_colors(statusLayer, GColorClear, GColorBlack);
	layer_add_child(window_layer, status_bar_layer_get_layer(statusLayer));
}

// Frees the window's layers. The pinned stops live in `pinStops`, so the menu is
// rebuilt from them on the next reveal. Freeing the layers while another window is
// on top keeps aplite's small heap available for that window.
static void pins_destroy_ui(void)
{
	if (pinsMenuLayer) {
		menu_layer_destroy(pinsMenuLayer);
		pinsMenuLayer = NULL;
	}
	if (statusLayer) {
		status_bar_layer_destroy(statusLayer);
		statusLayer = NULL;
	}
	if (loadingLayer) {
		text_layer_destroy(loadingLayer);
		loadingLayer = NULL;
	}
	if (titleLayer) {
		text_layer_destroy(titleLayer);
		titleLayer = NULL;
	}
}

void pins_window_load(Window *window)
{
	pinAmount = 0;
	pins_build_ui(window);

	pins_set_loading(true);

	pins_window_register_inbox();
	pin_transfer_started = false;

	// This load re-fetches the authoritative list, so any pending change flag from
	// pinning elsewhere is superseded; clear it so the first appear (which fires
	// while this fetch is still in flight) does not prune the not-yet-loaded list.
	pins_consume_changed();

	// With no pins there is nothing to fetch; show the empty state instead.
	if (pins_count() == 0) {
		text_layer_set_text(loadingLayer, "No pinned stops");
		return;
	}

	char codes[256];
	pins_build_codes_csv(codes, sizeof(codes));

	DictionaryIterator *iter;
	app_message_outbox_begin(&iter);
	if (iter == NULL) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "DictionaryIterator NULL!");
		text_layer_set_text(loadingLayer, "Couldn't load");
		return;
	}
	dict_write_cstring(iter, MESSAGE_KEY_pinMessage, codes);
	dict_write_end(iter);
	app_message_outbox_send();
}

// Drops any rows whose stop is no longer pinned, keeping the remaining rows in
// their existing (nearest-first) order. Used to refresh the visible list after a
// stop is unpinned without re-fetching from the network.
static void pins_prune_removed(void)
{
	int write = 0;
	for (int read = 0; read < pinAmount; ++read) {
		if (pins_is_pinned(pinStops[read].code)) {
			if (write != read) {
				pinStops[write] = pinStops[read];
			}
			++write;
		}
	}
	pinAmount = write;
	if (pinsMenuLayer) {
		menu_layer_reload_data(pinsMenuLayer);
	}

	// Fall back to the empty state once the last pin is gone.
	if (pinAmount == 0) {
		if (loadingLayer) {
			text_layer_set_text(loadingLayer, "No pinned stops");
		}
		pins_set_loading(true);
	}
}

// Re-registers the pinned stops inbox handler whenever the window is revealed,
// including after the departures or feedback window (pushed on top) is dismissed.
// If the layers were freed while covered, rebuild them and re-render the retained
// list; if a stop was unpinned while covered, the list is refreshed to match.
void pins_window_appear(Window *window)
{
	if (!pinsMenuLayer) {
		pins_build_ui(window);
		menu_layer_reload_data(pinsMenuLayer);
		pins_set_loading(pinAmount == 0);
		// No rows to show (e.g. everything was unpinned): show the empty state.
		if (pinAmount == 0 && loadingLayer) {
			text_layer_set_text(loadingLayer, "No pinned stops");
		}
	}

	pins_window_register_inbox();

	if (pins_consume_changed()) {
		pins_prune_removed();
	}
}

// Free the layers whenever another window covers this one, returning their memory
// to the heap for the window on top.
void pins_window_disappear(Window *window)
{
	pins_destroy_ui();
}

void pins_window_unload(Window *window)
{
	pins_destroy_ui();
}

void pins_window_create(void)
{
	pinsWindow = window_create();
	window_set_window_handlers(pinsWindow, (WindowHandlers) {
		.load = pins_window_load,
		.appear = pins_window_appear,
		.disappear = pins_window_disappear,
		.unload = pins_window_unload
	});
}

void pins_window_destroy(void)
{
	window_destroy(pinsWindow);
}

Window *pins_window_get_window(void)
{
	return pinsWindow;
}

void pins_window_show(void)
{
	window_stack_push(pinsWindow, true);
}
