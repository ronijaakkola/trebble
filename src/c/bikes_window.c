#include <pebble.h>
#include "bikes_window.h"
#include "error_window.h"
#include "marquee.h"

// City Bikes is not built on aplite — see the note in bikes_window.h. The whole file
// (and its create/destroy calls in main.c) is compiled out there to reclaim the RAM,
// so aplite carries none of this code.
#ifndef PBL_PLATFORM_APLITE

// Light-gray selection on color watches (keeps the black row text legible); B&W
// watches use the classic inverted black selection instead. Mirrors main_window.
#define BIKES_HL_COLOR GColorLightGray

static Window *bikesWindow;
static MenuLayer *bikesMenuLayer;
static StatusBarLayer *statusLayer;
static TextLayer *loadingLayer;
static TextLayer *titleLayer;

static int bikeAmount = 0;
static struct BikeStationInfo bike_stations[NUM_BIKE_STATIONS];
static bool bike_transfer_started;
static int bike_index;

// True once a transfer has completed, so we can tell "still loading" apart from
// "loaded but found nothing" and show the right centered message.
static bool bikes_loaded;

// Remembers the highlighted row while the menu layer is freed (e.g. another window
// on top) so returning to this list restores the row rather than resetting to top.
static uint16_t savedSelectedRow = 0;

// Shows the title + centered "Loading.." text while waiting for data, or hides them
// to reveal the populated menu (whose own section header takes over).
static void bikes_set_loading(bool loading)
{
	if (!loadingLayer || !titleLayer) {
		return;
	}
	layer_set_hidden(text_layer_get_layer(loadingLayer), !loading);
	layer_set_hidden(text_layer_get_layer(titleLayer), !loading);
}

static void process_bike_tuple(Tuple *t)
{
	uint32_t key = t->key;

	if (key == MESSAGE_KEY_bikeCode) {
		// stationId is sent for forward-compatibility (pinning, Phase 4) but is not
		// stored or used in v1 — see BikeStationInfo in the header.
	}
	else if (key == MESSAGE_KEY_bikeName) {
		strncpy(bike_stations[bike_index].name, t->value->cstring, sizeof(bike_stations[bike_index].name) - 1);
		bike_stations[bike_index].name[sizeof(bike_stations[bike_index].name) - 1] = '\0';
	}
	else if (key == MESSAGE_KEY_bikeDist) {
		bike_stations[bike_index].dist = t->value->int32;
	}
	else if (key == MESSAGE_KEY_bikeBikes) {
		bike_stations[bike_index].bikes = t->value->int32;
	}
	else if (key == MESSAGE_KEY_bikeMessage) {
		// Per-station marker; index bookkeeping happens in the inbox handler.
	}
	else {
		APP_LOG(APP_LOG_LEVEL_ERROR, "BikesWindow: AppMessage contained obscure key!");
	}
}

void bikes_message_inbox(DictionaryIterator *iter, void *context)
{
	// Each station arrives as its own message tagged with bikeMessage. We collect
	// all of its fields then advance the index once, so field order does not matter.
	if (dict_find(iter, MESSAGE_KEY_bikeMessage)) {
		if (!bike_transfer_started) {
			APP_LOG(APP_LOG_LEVEL_INFO, "BikesWindow: Started bike message transfer.");
			bike_transfer_started = true;
			bike_index = 0;
		}
		if (bike_index >= NUM_BIKE_STATIONS) {
			return;
		}
		// Default the optional count to "unknown" in case the key is missing.
		bike_stations[bike_index].bikes = -1;
		for (Tuple *t = dict_read_first(iter); t != NULL; t = dict_read_next(iter)) {
			process_bike_tuple(t);
		}
		++bike_index;
		return;
	}

	if (dict_find(iter, MESSAGE_KEY_messageEnd)) {
		APP_LOG(APP_LOG_LEVEL_INFO, "BikesWindow: Bike message transfer ended. %d stations.", bike_index);
		bike_transfer_started = false;
		bikeAmount = bike_index;
		bikes_loaded = true;
		if (bikesMenuLayer) {
			menu_layer_reload_data(bikesMenuLayer);
		}
		// No nearby stations: keep the centered overlay visible but swap "Loading.."
		// for an empty-state message, matching the nearby-stops pattern.
		if (bikeAmount == 0 && loadingLayer) {
			text_layer_set_text(loadingLayer, "No bike stations nearby");
		}
		bikes_set_loading(bikeAmount == 0);
		vibes_short_pulse();
		return;
	}

	if (dict_find(iter, MESSAGE_KEY_noInternet)) {
		APP_LOG(APP_LOG_LEVEL_WARNING, "JS reported no internet connection!");
		error_window_set_error("No internet connection", ERROR_ICON_NO_INTERNET);
		error_window_show();
		return;
	}

	if (dict_find(iter, MESSAGE_KEY_bikeNoFound)) {
		APP_LOG(APP_LOG_LEVEL_WARNING, "JS component was not able to find bike stations!");
		error_window_set_error("Was not able to find bike stations.", ERROR_ICON_GENERIC);
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

uint16_t bikes_menu_get_num_sections_callback(MenuLayer *menu_layer, void *data)
{
	return 1;
}

uint16_t bikes_menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
	switch (section_index) {
		case 0:
			return bikeAmount;
		default:
			return 0;
	}
}

int16_t bikes_menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
	return MENU_CELL_BASIC_HEADER_HEIGHT;
}

void bikes_menu_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data)
{
	if (section_index != 0) {
		return;
	}
	GRect bounds = layer_get_bounds(cell_layer);
	graphics_context_set_text_color(ctx, GColorBlack);
	graphics_draw_text(ctx, "Nearby bike stations", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

#ifdef PBL_ROUND
// Round carries "Nearby bike stations" in a fixed bar pinned below the status bar (rather than
// a scroll-away section header) so it stays visible as the list scrolls. The bar is
// opaque so it masks the loading title beneath it.
#define ROUND_HEADER_HEIGHT 28
static Layer *headerLayer;

static void bikes_header_update_proc(Layer *layer, GContext *ctx)
{
	GRect b = layer_get_bounds(layer);
	graphics_context_set_fill_color(ctx, GColorWhite);
	graphics_fill_rect(ctx, b, 0, GCornerNone);
	graphics_context_set_text_color(ctx, GColorBlack);
	graphics_draw_text(ctx, "Nearby bike stations", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(0, (b.size.h - 20) / 2, b.size.w, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}
#endif

int16_t bikes_menu_get_cell_height_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
#ifdef PBL_PLATFORM_EMERY
	return 50;
#elif defined(PBL_ROUND)
	return 54;
#else
	return 48;
#endif
}

// Formats the available-bikes count, or "n/a" when it is unknown (the station is not
// reporting real-time data). Returns true when a number (and thus the "bikes" unit
// label) should be shown.
static bool format_bike_count(const struct BikeStationInfo *station, char *out, size_t out_size)
{
	if (station->bikes < 0) {
		strncpy(out, "n/a", out_size - 1);
		out[out_size - 1] = '\0';
		return false;
	}
	snprintf(out, out_size, "%d", station->bikes);
	return true;
}

#ifdef PBL_COLOR
// Color of the available-bikes number on color watches: gray when empty, red when
// scarce (1–3), green when there are more. On B&W watches there is no color, so the
// number just uses the row text color.
static GColor bike_count_color(int bikes, bool highlighted)
{
	if (bikes == 0) {
		return GColorDarkGray;
	}
	if (bikes <= 3) {
		return GColorRed;
	}
	// Darker green on the light-gray highlight so it stays legible, mirroring the
	// departures screen's realtime-green treatment.
	return highlighted ? GColorDarkGreen : GColorIslamicGreen;
}
#endif

void bikes_menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data)
{
	if (cell_index->section != 0) {
		return;
	}

	struct BikeStationInfo *station = &bike_stations[cell_index->row];
	GRect bounds = layer_get_bounds(cell_layer);

	char count_str[12];
	bool has_count = format_bike_count(station, count_str, sizeof(count_str));

	char dist_str[12];
	snprintf(dist_str, sizeof(dist_str), "%d m", station->dist);

#ifdef PBL_ROUND
	{
	// Round display: everything centered so nothing is clipped by the circular bezel.
	// Name on top, with the distance and bikes count centered together below it.
	int16_t cy = bounds.size.h / 2;

	graphics_context_set_text_color(ctx, GColorBlack);
	graphics_draw_text(ctx, station->name, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(6, cy - 22, bounds.size.w - 12, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

	char meta[32];
	if (has_count) {
		snprintf(meta, sizeof(meta), "%s, %s bikes", dist_str, count_str);
	} else {
		snprintf(meta, sizeof(meta), "%s, n/a", dist_str);
	}
	graphics_context_set_text_color(ctx, GColorDarkGray);
	graphics_draw_text(ctx, meta, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(6, cy + 4, bounds.size.w - 12, 18), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
	return;
	}
#endif

	#ifdef PBL_COLOR
		GColor text_color = GColorBlack;
	#else
		GColor text_color = menu_cell_layer_is_highlighted(cell_layer) ? GColorWhite : GColorBlack;
	#endif
	// The actual row background, used to mask the count column and the marquee gutter:
	// the highlight color on the focused row, white otherwise (black on B&W).
	GColor row_bg = menu_cell_layer_is_highlighted(cell_layer)
		? COLOR_FALLBACK(BIKES_HL_COLOR, GColorBlack)
		: GColorWhite;

	int16_t cy = bounds.size.h / 2;
	int16_t pad = 8; // match the nearby-stops left inset
	GColor meta_color = COLOR_FALLBACK(GColorDarkGray, text_color);

	// Right-hand count column: the available-bikes number over a small "bikes" unit (or
	// "n/a" alone). Reserve only its measured width so the name keeps the rest of the row.
	GFont num_font = has_count ? fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD)
	                           : fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
	GSize num_sz = graphics_text_layout_get_content_size(count_str, num_font, GRect(0, 0, bounds.size.w, 40), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
	int16_t col_w = num_sz.w;
	if (has_count) {
		GSize unit_sz = graphics_text_layout_get_content_size("bikes", fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(0, 0, bounds.size.w, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
		if (unit_sz.w > col_w) {
			col_w = unit_sz.w;
		}
	}
	int16_t col_right = bounds.size.w - pad;
	int16_t col_left = col_right - col_w;
	int16_t name_right = col_left - 6; // gap between the name and the count

	// Name over distance on the left — identical fonts/spacing to the nearby-stops list
	// (GOTHIC_18_BOLD name at cy-21, GOTHIC_18 distance at cy). The name scrolls when
	// focused; its scrolled-off tail is masked out of the count column below, the same
	// way the nearby-stops name slides under its mode badge.
	marquee_draw_label(ctx, cell_layer, station->name, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), text_color, GRect(pad, cy - 21, name_right - pad, 22), row_bg);
	graphics_context_set_text_color(ctx, text_color);
	graphics_draw_text(ctx, dist_str, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(pad, cy, name_right - pad, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

	// marquee_draw_label only masks its left gutter, so mask the count column with the
	// row background before painting the count over it — otherwise a long scrolling name
	// overruns the number.
	graphics_context_set_fill_color(ctx, row_bg);
	graphics_fill_rect(ctx, GRect(name_right, 0, bounds.size.w - name_right, bounds.size.h), 0, GCornerNone);

	if (has_count) {
		// Number over a small grey "bikes" unit, centered as one group around the row
		// midline — mirrors the departures screen's time-over-countdown stacking. The
		// number is colored by availability on color watches (gray/red/green).
#ifdef PBL_COLOR
		graphics_context_set_text_color(ctx, bike_count_color(station->bikes, menu_cell_layer_is_highlighted(cell_layer)));
#else
		graphics_context_set_text_color(ctx, text_color);
#endif
		graphics_draw_text(ctx, count_str, num_font, GRect(col_left, cy - 22, col_w, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
		graphics_context_set_text_color(ctx, meta_color);
		graphics_draw_text(ctx, "bikes", fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(col_left, cy, col_w, 14), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
	} else {
		graphics_context_set_text_color(ctx, text_color);
		graphics_draw_text(ctx, count_str, num_font, GRect(col_left, cy - 11, col_w, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
	}
}

void bikes_setup_menu_layer(Window *window, Layer *window_layer)
{
	GRect window_bounds = layer_get_bounds(window_layer);
	window_bounds.origin.y = STATUS_BAR_LAYER_HEIGHT;
#ifdef PBL_ROUND
	window_bounds.origin.y += ROUND_HEADER_HEIGHT;
	window_bounds.size.h -= ROUND_HEADER_HEIGHT;
#endif

	bikesMenuLayer = menu_layer_create(window_bounds);
	menu_layer_set_callbacks(bikesMenuLayer, NULL, (MenuLayerCallbacks){
		#ifdef PBL_RECT
		 .get_num_sections = bikes_menu_get_num_sections_callback,
		 .get_header_height = bikes_menu_get_header_height_callback,
		 .draw_header = bikes_menu_draw_header_callback,
		#endif
		.get_num_rows = bikes_menu_get_num_rows_callback,
		.get_cell_height = bikes_menu_get_cell_height_callback,
		.draw_row = bikes_menu_draw_row_callback,
		.selection_changed = marquee_selection_changed,
	});

	menu_layer_set_highlight_colors(bikesMenuLayer, COLOR_FALLBACK(BIKES_HL_COLOR, GColorBlack), COLOR_FALLBACK(GColorBlack, GColorWhite));
	menu_layer_set_click_config_onto_window(bikesMenuLayer, window);

	layer_add_child(window_layer, menu_layer_get_layer(bikesMenuLayer));
}

// Title + centered "Loading.." text, shown over the (empty) menu until stations
// arrive (or, on an empty result, swapped for the empty-state message).
void bikes_setup_loading_layer(Layer *window_layer)
{
	GRect bounds = layer_get_bounds(window_layer);
	int16_t top = STATUS_BAR_LAYER_HEIGHT;

	titleLayer = text_layer_create(GRect(4, top, bounds.size.w - 8, 18));
	text_layer_set_background_color(titleLayer, GColorClear);
	text_layer_set_text_color(titleLayer, GColorBlack);
	text_layer_set_font(titleLayer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
	text_layer_set_text_alignment(titleLayer, GTextAlignmentCenter);
	text_layer_set_text(titleLayer, "Nearby bike stations");
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

static void bikes_build_ui(Window *window)
{
	if (bikesMenuLayer) {
		return;
	}
	Layer *window_layer = window_get_root_layer(window);

	bikes_setup_menu_layer(window, window_layer);
	bikes_setup_loading_layer(window_layer);

#ifdef PBL_ROUND
	GRect hb = layer_get_bounds(window_layer);
	headerLayer = layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT, hb.size.w, ROUND_HEADER_HEIGHT));
	layer_set_update_proc(headerLayer, bikes_header_update_proc);
	layer_add_child(window_layer, headerLayer);
#endif

	statusLayer = status_bar_layer_create();
	status_bar_layer_set_separator_mode(statusLayer, StatusBarLayerSeparatorModeDotted);
	status_bar_layer_set_colors(statusLayer, GColorClear, GColorBlack);
	layer_add_child(window_layer, status_bar_layer_get_layer(statusLayer));
}

static void bikes_destroy_ui(void)
{
	if (bikesMenuLayer) {
		savedSelectedRow = menu_layer_get_selected_index(bikesMenuLayer).row;
		menu_layer_destroy(bikesMenuLayer);
		bikesMenuLayer = NULL;
	}
#ifdef PBL_ROUND
	if (headerLayer) {
		layer_destroy(headerLayer);
		headerLayer = NULL;
	}
#endif
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

void bikes_window_load(Window *window)
{
	// Clear any stations left over from a previous load so the menu does not render
	// stale rows behind the loading overlay.
	bikeAmount = 0;
	bikes_loaded = false;
	bikes_build_ui(window);
	bikes_set_loading(true);

	app_message_register_inbox_received(bikes_message_inbox);
	bike_transfer_started = false;
	// Reset the per-transfer index too: an empty result sends messageEnd with no
	// bikeMessage to reset it, so a stale count would otherwise be read as the total.
	bike_index = 0;

	DictionaryIterator *iter;
	app_message_outbox_begin(&iter);
	if (iter == NULL) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "DictionaryIterator NULL!");
		return;
	}
	dict_write_uint16(iter, MESSAGE_KEY_bikeMessage, 1);
	dict_write_end(iter);
	app_message_outbox_send();
}

void bikes_window_appear(Window *window)
{
	if (!bikesMenuLayer) {
		bikes_build_ui(window);
		menu_layer_reload_data(bikesMenuLayer);
		if (bikes_loaded && bikeAmount == 0 && loadingLayer) {
			text_layer_set_text(loadingLayer, "No bike stations nearby");
		}
		bikes_set_loading(bikeAmount == 0);
		if (bikeAmount > 0) {
			uint16_t row = savedSelectedRow < (uint16_t)bikeAmount ? savedSelectedRow : (uint16_t)(bikeAmount - 1);
			menu_layer_set_selected_index(bikesMenuLayer, MenuIndex(0, row), MenuRowAlignCenter, false);
		}
	}

	// Reclaim the AppMessage inbox (the home window held it while this was pushed).
	app_message_register_inbox_received(bikes_message_inbox);

	// Drive the scrolling station names while this list is on screen.
	marquee_attach(bikesMenuLayer);
}

void bikes_window_disappear(Window *window)
{
	marquee_detach(bikesMenuLayer);
	bikes_destroy_ui();
}

void bikes_window_unload(Window *window)
{
	marquee_detach(bikesMenuLayer);
	bikes_destroy_ui();
}

void bikes_window_create()
{
	bikesWindow = window_create();
	window_set_window_handlers(bikesWindow, (WindowHandlers) {
		.load = bikes_window_load,
		.appear = bikes_window_appear,
		.disappear = bikes_window_disappear,
		.unload = bikes_window_unload
	});
}

void bikes_window_destroy()
{
	window_destroy(bikesWindow);
}

Window *bikes_window_get_window()
{
	return bikesWindow;
}

#endif // !PBL_PLATFORM_APLITE
