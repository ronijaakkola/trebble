#include <pebble.h>
#include "main_window.h"
#include "lines_window.h"
#include "error_window.h"
#include "pins.h"

static Window *mainWindow;
static MenuLayer *mainMenuLayer;
static StatusBarLayer *statusLayer;
static TextLayer *loadingLayer;
static TextLayer *titleLayer;

static int stopAmount = 0;
static struct StopInfo stops[NUM_STOPS];
static bool stop_transfer_started;
static int stop_index;

// Remembers the highlighted row while the menu layer is freed (e.g. with the
// departures window on top) so returning to this list restores the tapped stop
// rather than resetting to the top.
static uint16_t savedSelectedRow = 0;

// Shows the title + centered "Loading.." text while waiting for data, or hides
// them to reveal the populated stops menu (whose own section header takes over).
static void main_set_loading(bool loading)
{
	// The overlay layers only exist while the window is built (see main_build_ui).
	if (!loadingLayer || !titleLayer) {
		return;
	}
	layer_set_hidden(text_layer_get_layer(loadingLayer), !loading);
	layer_set_hidden(text_layer_get_layer(titleLayer), !loading);
}

// Maps a Digitransit vehicle mode string to a single-letter type indicator.
// Unknown modes produce an empty string so no icon is shown.
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

static void process_stop_tuple(Tuple *t)
{
	uint32_t key = t->key;

	if (key == MESSAGE_KEY_stopCode) {
		strncpy(stops[stop_index].code, t->value->cstring, sizeof(stops[stop_index].code) - 1);
		stops[stop_index].code[sizeof(stops[stop_index].code) - 1] = '\0';
	}
	else if (key == MESSAGE_KEY_stopName) {
		strncpy(stops[stop_index].name, t->value->cstring, sizeof(stops[stop_index].name) - 1);
		stops[stop_index].name[sizeof(stops[stop_index].name) - 1] = '\0';
	}
	else if (key == MESSAGE_KEY_stopDist) {
		stops[stop_index].dist = t->value->int32;
	}
	else if (key == MESSAGE_KEY_stopMode) {
		mode_to_type_letter(t->value->cstring, stops[stop_index].type);
	}
	else if (key == MESSAGE_KEY_stopMessage) {
		// Per-stop marker; the index bookkeeping happens in the inbox handler.
	}
	else {
		APP_LOG(APP_LOG_LEVEL_ERROR, "MainWindow: AppMessage contained obscure key!");
	}
}

void main_message_inbox(DictionaryIterator *iter, void *context)
{
	// Each stop arrives as its own message, tagged with stopMessage. We collect
	// all of its fields and then advance the index once, so field order within
	// the message does not matter.
	if (dict_find(iter, MESSAGE_KEY_stopMessage)) {
		if (!stop_transfer_started) {
			APP_LOG(APP_LOG_LEVEL_INFO, "MainWindow: Started stop message transfer.");
			stop_transfer_started = true;
			stop_index = 0;
		}
		if (stop_index >= NUM_STOPS) {
			return;
		}
		// Clear the optional type since a stop without a known mode sends no key.
		stops[stop_index].type[0] = '\0';
		for (Tuple *t = dict_read_first(iter); t != NULL; t = dict_read_next(iter)) {
			process_stop_tuple(t);
		}
		++stop_index;
		return;
	}

	if (dict_find(iter, MESSAGE_KEY_messageEnd)) {
		APP_LOG(APP_LOG_LEVEL_INFO, "MainWindow: Stop message transfer ended. Total of %d stops were transfered.", stop_index);
		stop_transfer_started = false;
		stopAmount = stop_index;
		// The list may have been freed if the window was covered before the data
		// arrived; the count is retained so it renders when the window reappears.
		if (mainMenuLayer) {
			menu_layer_reload_data(mainMenuLayer);
		}
		main_set_loading(false);
		vibes_short_pulse();
		return;
	}

	if (dict_find(iter, MESSAGE_KEY_noInternet)) {
		APP_LOG(APP_LOG_LEVEL_WARNING, "JS reported no internet connection!");
		error_window_set_error("No internet connection", ERROR_ICON_NO_INTERNET);
		error_window_show();
		return;
	}

	if (dict_find(iter, MESSAGE_KEY_stopNoFound)) {
		APP_LOG(APP_LOG_LEVEL_WARNING, "JS component was not able to find stops!");
		error_window_set_error("Was not able to find nearby stops.", ERROR_ICON_GENERIC);
		error_window_show();
		return;
	}

	if (dict_find(iter, MESSAGE_KEY_noGps)) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "JS reported that location was not found!");
		error_window_set_error("Was not able to determine your location.", ERROR_ICON_GENERIC);
		error_window_show();
		return;
	}
}

void main_window_register_inbox()
{
	app_message_register_inbox_received(main_message_inbox);
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
	if (section_index != 0) {
		return;
	}
	// Centered header, matching the centered title shown during loading so the
	// title does not shift when the list appears.
	GRect bounds = layer_get_bounds(cell_layer);
	graphics_context_set_text_color(ctx, GColorBlack);
	graphics_draw_text(ctx, "Nearby stops", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

int16_t menu_get_cell_height_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	return 48; // A bit taller than the Pencil design's 46 for breathing room
}

void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data)
{
	if (cell_index->section != 0) {
		return;
	}

	struct StopInfo *stop = &stops[cell_index->row];
	GRect bounds = layer_get_bounds(cell_layer);

	#ifdef PBL_COLOR
		GColor text_color = GColorBlack;
	#else
		GColor text_color = menu_cell_layer_is_highlighted(cell_layer) ? GColorWhite : GColorBlack;
	#endif

	// Type badge on the left, vertically centered: a small rounded rectangle with a
	// white letter inside (B for bus, T for tram). On color watches it is colored
	// by type (blue for bus, red for tram) and stays colored even when selected,
	// since the light-gray highlight keeps it legible. On B&W watches the black
	// badge inverts to a white badge with a black letter on the solid-black
	// selected row so it stays visible.
	int16_t cy = bounds.size.h / 2;
	int16_t text_x = 8;
	if (stop->type[0] == 'B' || stop->type[0] == 'T') {
		#ifdef PBL_COLOR
			GColor badge_color = stop->type[0] == 'B' ? GColorCobaltBlue : GColorRed;
			GColor letter_color = GColorWhite;
		#else
			bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
			GColor badge_color = highlighted ? GColorWhite : GColorBlack;
			GColor letter_color = highlighted ? GColorBlack : GColorWhite;
		#endif

		const int16_t badge_size = 18;
		GRect badge = GRect(6, cy - badge_size / 2, badge_size, badge_size);
		graphics_context_set_fill_color(ctx, badge_color);
		graphics_fill_rect(ctx, badge, 4, GCornersAll);

		// Pebble draws glyphs near the top of their box, so nudge the centered
		// letter down to sit in the middle of the badge.
		char letter[2] = { stop->type[0], '\0' };
		graphics_context_set_text_color(ctx, letter_color);
		graphics_draw_text(ctx, letter, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GRect(badge.origin.x, badge.origin.y - 1, badge_size, badge_size), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

		text_x = 6 + badge_size + 8;
	}

	int16_t text_w = bounds.size.w - text_x - 4;

	graphics_context_set_text_color(ctx, text_color);
	graphics_draw_text(ctx, stop->name, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(text_x, cy - 21, text_w, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

	char dist[12];
	snprintf(dist, sizeof(dist), "%d m", stop->dist);
	graphics_draw_text(ctx, dist, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(text_x, cy, text_w, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	lines_window_show(stops[cell_index->row].code, stops[cell_index->row].name, stops[cell_index->row].type);
}

// Long-pressing a stop opens an action menu to pin/unpin it.
void menu_long_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	struct StopInfo *stop = &stops[cell_index->row];
	pins_show_action_menu(stop->code, stop->name, stop->type, "Pin stop", "Unpin stop");
}

void setup_menu_layer(Window *window, Layer *window_layer)
{
	GRect window_bounds = layer_get_bounds(window_layer);
	window_bounds.origin.y = STATUS_BAR_LAYER_HEIGHT;

	mainMenuLayer = menu_layer_create(window_bounds);
	// This has to be done slightly differently for Pebble Round
	menu_layer_set_callbacks(mainMenuLayer, NULL, (MenuLayerCallbacks){
		#ifdef PBL_RECT
		 .get_num_sections = menu_get_num_sections_callback,
		 .get_header_height = menu_get_header_height_callback,
		 .draw_header = menu_draw_header_callback,
		#endif
		.get_num_rows = menu_get_num_rows_callback,
		.get_cell_height = menu_get_cell_height_callback,
		.draw_row = menu_draw_row_callback,
		.select_click = menu_select_callback,
		.select_long_click = menu_long_select_callback,
	});

	// Color watches use the light-gray selection (black text/icons stay legible);
	// B&W watches use the classic inverted black selection, where the row icons and
	// text flip to white when highlighted.
	menu_layer_set_highlight_colors(mainMenuLayer, COLOR_FALLBACK(MENU_HL_COLOR, GColorBlack), COLOR_FALLBACK(GColorBlack, GColorWhite));
	menu_layer_set_click_config_onto_window(mainMenuLayer, window);

	layer_add_child(window_layer, menu_layer_get_layer(mainMenuLayer));
}

// Title + centered "Loading.." text, shown over the (empty) menu until stops
// arrive. The MenuLayer does not draw its section header while it has no rows,
// so the title is shown here explicitly during the loading state.
void setup_loading_layer(Layer *window_layer)
{
	GRect bounds = layer_get_bounds(window_layer);
	int16_t top = STATUS_BAR_LAYER_HEIGHT;

	GTextAlignment title_align = GTextAlignmentCenter;
	titleLayer = text_layer_create(GRect(4, top, bounds.size.w - 8, 18));
	text_layer_set_background_color(titleLayer, GColorClear);
	text_layer_set_text_color(titleLayer, GColorBlack);
	text_layer_set_font(titleLayer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
	text_layer_set_text_alignment(titleLayer, title_align);
	text_layer_set_text(titleLayer, "Nearby stops");
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
// being covered (see main_window_appear/disappear). No-op if already built.
static void main_build_ui(Window *window)
{
	if (mainMenuLayer) {
		return;
	}
	Layer *window_layer = window_get_root_layer(window);

	setup_menu_layer(window, window_layer);
	setup_loading_layer(window_layer);

	statusLayer = status_bar_layer_create();
	status_bar_layer_set_separator_mode(statusLayer, StatusBarLayerSeparatorModeDotted);
	status_bar_layer_set_colors(statusLayer, GColorClear, GColorBlack);
	layer_add_child(window_layer, status_bar_layer_get_layer(statusLayer));
}

// Frees the window's layers. The stop list itself lives in `stops`, so the menu
// is rebuilt from it on the next reveal. Freeing the layers while another window
// is on top keeps aplite's small heap available for that window.
static void main_destroy_ui(void)
{
	if (mainMenuLayer) {
		savedSelectedRow = menu_layer_get_selected_index(mainMenuLayer).row;
		menu_layer_destroy(mainMenuLayer);
		mainMenuLayer = NULL;
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

void main_window_load(Window *window)
{
	// Clear any stops left over from a previous load so the menu does not render
	// stale rows (and its header) behind the loading overlay.
	stopAmount = 0;
	main_build_ui(window);
	main_set_loading(true);

	// Request nearby stops from the JS component.
	app_message_register_inbox_received(main_message_inbox);
	stop_transfer_started = false;

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

// Re-claims the AppMessage inbox whenever the stops window is revealed, including
// after the departures window (pushed on top) is dismissed. If the layers were
// freed while covered, rebuild them and re-render the retained stop list.
void main_window_appear(Window *window)
{
	if (!mainMenuLayer) {
		main_build_ui(window);
		menu_layer_reload_data(mainMenuLayer);
		main_set_loading(stopAmount == 0);
		// Restore the row the user left on, clamped in case the list shrank.
		if (stopAmount > 0) {
			uint16_t row = savedSelectedRow < (uint16_t)stopAmount ? savedSelectedRow : (uint16_t)(stopAmount - 1);
			menu_layer_set_selected_index(mainMenuLayer, MenuIndex(0, row), MenuRowAlignCenter, false);
		}
	}

	main_window_register_inbox();
}

// Free the layers whenever another window covers this one, returning their memory
// to the heap for the window on top.
void main_window_disappear(Window *window)
{
	main_destroy_ui();
}

void main_window_unload(Window *window)
{
	main_destroy_ui();
}

void main_window_create()
{
	mainWindow = window_create();
	window_set_window_handlers(mainWindow, (WindowHandlers) {
		.load = main_window_load,
		.appear = main_window_appear,
		.disappear = main_window_disappear,
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
