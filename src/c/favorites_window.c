#include <pebble.h>
#include "favorites_window.h"
#include "favorites.h"
#include "main_window.h"   // struct StopInfo, NUM_STOPS
#include "lines_window.h"
#include "error_window.h"

static Window *favoritesWindow;
static MenuLayer *favMenuLayer;
static StatusBarLayer *statusLayer;
static TextLayer *loadingLayer;
static TextLayer *titleLayer;

static int favAmount = 0;
static struct StopInfo favStops[NUM_STOPS];
static bool fav_transfer_started;
static int fav_index;

// Shows the title + centered status text (loading / empty / error), or hides
// them to reveal the populated favorites menu (whose own header takes over).
static void favorites_set_loading(bool loading)
{
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

static void process_fav_tuple(Tuple *t)
{
	uint32_t key = t->key;

	if (key == MESSAGE_KEY_favCode) {
		strncpy(favStops[fav_index].code, t->value->cstring, sizeof(favStops[fav_index].code) - 1);
		favStops[fav_index].code[sizeof(favStops[fav_index].code) - 1] = '\0';
	}
	else if (key == MESSAGE_KEY_favName) {
		strncpy(favStops[fav_index].name, t->value->cstring, sizeof(favStops[fav_index].name) - 1);
		favStops[fav_index].name[sizeof(favStops[fav_index].name) - 1] = '\0';
	}
	else if (key == MESSAGE_KEY_favDist) {
		favStops[fav_index].dist = t->value->int32;
	}
	else if (key == MESSAGE_KEY_favMode) {
		mode_to_type_letter(t->value->cstring, favStops[fav_index].type);
	}
	else if (key == MESSAGE_KEY_favMessage) {
		// Per-favorite marker; index bookkeeping happens in the inbox handler.
	}
}

void favorites_message_inbox(DictionaryIterator *iter, void *context)
{
	// Each favorite arrives as its own message tagged with favMessage, ordered
	// nearest-first by the JS component.
	if (dict_find(iter, MESSAGE_KEY_favMessage)) {
		if (!fav_transfer_started) {
			fav_transfer_started = true;
			fav_index = 0;
		}
		if (fav_index >= NUM_STOPS) {
			return;
		}
		// Defaults for optional fields a favorite may omit.
		favStops[fav_index].type[0] = '\0';
		favStops[fav_index].dist = -1;
		for (Tuple *t = dict_read_first(iter); t != NULL; t = dict_read_next(iter)) {
			process_fav_tuple(t);
		}
		++fav_index;
		return;
	}

	if (dict_find(iter, MESSAGE_KEY_messageEnd)) {
		fav_transfer_started = false;
		favAmount = fav_index;
		menu_layer_reload_data(favMenuLayer);
		favorites_set_loading(false);
		vibes_short_pulse();
		return;
	}

	if (dict_find(iter, MESSAGE_KEY_favNoFound)) {
		APP_LOG(APP_LOG_LEVEL_WARNING, "JS could not load favorite stops!");
		fav_transfer_started = false;
		text_layer_set_text(loadingLayer, "Couldn't load");
		return;
	}

	if (dict_find(iter, MESSAGE_KEY_noGps)) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "JS reported that location was not found!");
		fav_transfer_started = false;
		text_layer_set_text(loadingLayer, "No location");
		return;
	}
}

void favorites_window_register_inbox(void)
{
	app_message_register_inbox_received(favorites_message_inbox);
}

uint16_t favorites_get_num_sections_callback(MenuLayer *menu_layer, void *data)
{
	return 1;
}

uint16_t favorites_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
	switch (section_index) {
		case 0:
			return favAmount;
		default:
			return 0;
	}
}

int16_t favorites_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
	return MENU_CELL_BASIC_HEADER_HEIGHT;
}

void favorites_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data)
{
	if (section_index != 0) {
		return;
	}
	GRect bounds = layer_get_bounds(cell_layer);
	graphics_context_set_text_color(ctx, GColorBlack);
	graphics_draw_text(ctx, "Favorites", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

int16_t favorites_get_cell_height_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	return 40; // Matches the nearby stops list.
}

void favorites_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data)
{
	if (cell_index->section != 0) {
		return;
	}

	struct StopInfo *stop = &favStops[cell_index->row];
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

	// The favorites list shows only the stop name (no distance), so it is
	// vertically centered in the cell. The stops are still ordered nearest-first
	// using the distance computed by the JS component.
	graphics_context_set_text_color(ctx, text_color);
	graphics_draw_text(ctx, stop->name, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(text_x, cy - 13, text_w, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

void favorites_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	lines_window_show(favStops[cell_index->row].code, favStops[cell_index->row].name);
}

void favorites_long_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	struct StopInfo *stop = &favStops[cell_index->row];
	favorites_show_action_menu(stop->code, stop->name, stop->type, "Favorite stop", "Unfavorite stop");
}

void setup_favorites_menu_layer(Window *window, Layer *window_layer)
{
	GRect window_bounds = layer_get_bounds(window_layer);
	window_bounds.origin.y = STATUS_BAR_LAYER_HEIGHT;

	favMenuLayer = menu_layer_create(window_bounds);
	menu_layer_set_callbacks(favMenuLayer, NULL, (MenuLayerCallbacks){
		#ifdef PBL_RECT
		 .get_num_sections = favorites_get_num_sections_callback,
		 .get_header_height = favorites_get_header_height_callback,
		 .draw_header = favorites_draw_header_callback,
		#endif
		.get_num_rows = favorites_get_num_rows_callback,
		.get_cell_height = favorites_get_cell_height_callback,
		.draw_row = favorites_draw_row_callback,
		.select_click = favorites_select_callback,
		.select_long_click = favorites_long_select_callback,
	});

	menu_layer_set_highlight_colors(favMenuLayer, COLOR_FALLBACK(FAV_HL_COLOR, GColorBlack), COLOR_FALLBACK(GColorBlack, GColorWhite));
	menu_layer_set_click_config_onto_window(favMenuLayer, window);

	layer_add_child(window_layer, menu_layer_get_layer(favMenuLayer));
}

// Title + centered status text, shown over the (empty) menu until favorites
// arrive (or to convey the empty / error state).
void setup_favorites_loading_layer(Layer *window_layer)
{
	GRect bounds = layer_get_bounds(window_layer);
	int16_t top = STATUS_BAR_LAYER_HEIGHT;

	titleLayer = text_layer_create(GRect(4, top, bounds.size.w - 8, 18));
	text_layer_set_background_color(titleLayer, GColorClear);
	text_layer_set_text_color(titleLayer, GColorBlack);
	text_layer_set_font(titleLayer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
	text_layer_set_text_alignment(titleLayer, GTextAlignmentCenter);
	text_layer_set_text(titleLayer, "Favorites");
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

void favorites_window_load(Window *window)
{
	Layer *window_layer = window_get_root_layer(window);

	favAmount = 0;
	setup_favorites_menu_layer(window, window_layer);
	setup_favorites_loading_layer(window_layer);

	statusLayer = status_bar_layer_create();
	status_bar_layer_set_separator_mode(statusLayer, StatusBarLayerSeparatorModeDotted);
	status_bar_layer_set_colors(statusLayer, GColorClear, GColorBlack);
	layer_add_child(window_layer, status_bar_layer_get_layer(statusLayer));

	favorites_set_loading(true);

	favorites_window_register_inbox();
	fav_transfer_started = false;

	// This load re-fetches the authoritative list, so any pending change flag from
	// favoriting elsewhere is superseded; clear it so the first appear (which fires
	// while this fetch is still in flight) does not prune the not-yet-loaded list.
	favorites_consume_changed();

	// With no favorites there is nothing to fetch; show the empty state instead.
	if (favorites_count() == 0) {
		text_layer_set_text(loadingLayer, "No favorites");
		return;
	}

	char codes[256];
	favorites_build_codes_csv(codes, sizeof(codes));

	DictionaryIterator *iter;
	app_message_outbox_begin(&iter);
	if (iter == NULL) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "DictionaryIterator NULL!");
		text_layer_set_text(loadingLayer, "Couldn't load");
		return;
	}
	dict_write_cstring(iter, MESSAGE_KEY_favMessage, codes);
	dict_write_end(iter);
	app_message_outbox_send();
}

// Drops any rows whose stop is no longer a favorite, keeping the remaining rows
// in their existing (nearest-first) order. Used to refresh the visible list after
// a stop is unfavorited without re-fetching from the network.
static void favorites_prune_removed(void)
{
	int write = 0;
	for (int read = 0; read < favAmount; ++read) {
		if (favorites_is_favorite(favStops[read].code)) {
			if (write != read) {
				favStops[write] = favStops[read];
			}
			++write;
		}
	}
	favAmount = write;
	menu_layer_reload_data(favMenuLayer);

	// Fall back to the empty state once the last favorite is gone.
	if (favAmount == 0) {
		text_layer_set_text(loadingLayer, "No favorites");
		favorites_set_loading(true);
	}
}

// Re-registers the favorites inbox handler whenever the window is revealed,
// including after the departures or feedback window (pushed on top) is dismissed.
// If a favorite was removed while covered, the list is refreshed to match.
void favorites_window_appear(Window *window)
{
	favorites_window_register_inbox();

	if (favorites_consume_changed()) {
		favorites_prune_removed();
	}
}

void favorites_window_unload(Window *window)
{
	menu_layer_destroy(favMenuLayer);
	status_bar_layer_destroy(statusLayer);
	text_layer_destroy(loadingLayer);
	text_layer_destroy(titleLayer);
}

void favorites_window_create(void)
{
	favoritesWindow = window_create();
	window_set_window_handlers(favoritesWindow, (WindowHandlers) {
		.load = favorites_window_load,
		.appear = favorites_window_appear,
		.unload = favorites_window_unload
	});
}

void favorites_window_destroy(void)
{
	window_destroy(favoritesWindow);
}

Window *favorites_window_get_window(void)
{
	return favoritesWindow;
}

void favorites_window_show(void)
{
	window_stack_push(favoritesWindow, true);
}
