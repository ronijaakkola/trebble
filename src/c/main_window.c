#include <pebble.h>
#include "main_window.h"
#include "lines_window.h"
#include "error_window.h"
#include "pins.h"
#include "marquee.h"
#include "region.h"

static Window *mainWindow;
static MenuLayer *mainMenuLayer;
static StatusBarLayer *statusLayer;
static TextLayer *loadingLayer;
static TextLayer *titleLayer;

#ifdef PBL_PLATFORM_EMERY
// Mode icons (bus/tram line art) drawn at the left of each row on emery, tinted
// per row to the mode color. Loaded with the window's other layers.
static GDrawCommandImage *busIcon;
static GDrawCommandImage *tramIcon;

// Recolors every command in a PDC image, used to tint the line-art mode icons to
// the mode color before drawing. Mirrors home_window.c's helper.
static void pdc_set_colors(GDrawCommandImage *image, GColor stroke, GColor fill)
{
	GDrawCommandList *list = gdraw_command_image_get_command_list(image);
	uint32_t num = gdraw_command_list_get_num_commands(list);
	for (uint32_t i = 0; i < num; ++i) {
		GDrawCommand *cmd = gdraw_command_list_get_command(list, i);
		gdraw_command_set_stroke_color(cmd, stroke);
		gdraw_command_set_fill_color(cmd, fill);
	}
}
#endif

static int stopAmount = 0;
static struct StopInfo stops[NUM_STOPS];
static bool stop_transfer_started;
static int stop_index;

// True once a stop transfer has completed, so we can tell "still loading" apart
// from "loaded but found nothing" and show the right centered message.
static bool stops_loaded;

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
		stops_loaded = true;
		// The list may have been freed if the window was covered before the data
		// arrived; the count is retained so it renders when the window reappears.
		if (mainMenuLayer) {
			menu_layer_reload_data(mainMenuLayer);
		}
		// No nearby stops were found: keep the centered overlay visible but swap
		// the "Loading.." text for an empty-state message (same font/centering),
		// matching the "No departures" pattern on the departures screen.
		if (stopAmount == 0 && loadingLayer) {
			text_layer_set_text(loadingLayer, "No nearby stops");
		}
		main_set_loading(stopAmount == 0);
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

#ifdef PBL_ROUND
// The round display carries "Nearby stops" in a fixed bar pinned below the status
// bar (rather than a scroll-away section header), so it stays visible as the list
// scrolls. The bar is opaque so it masks the loading title beneath it.
#define ROUND_HEADER_HEIGHT 28
static Layer *headerLayer;

static void main_header_update_proc(Layer *layer, GContext *ctx)
{
	GRect b = layer_get_bounds(layer);
	graphics_context_set_fill_color(ctx, GColorWhite);
	graphics_fill_rect(ctx, b, 0, GCornerNone);
	graphics_context_set_text_color(ctx, GColorBlack);
	graphics_draw_text(ctx, "Nearby stops", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(0, (b.size.h - 20) / 2, b.size.w, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}
#endif

int16_t menu_get_cell_height_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
#ifdef PBL_PLATFORM_EMERY
	// Emery rows stack the name over a meta line (stop-code badge + distance), so
	// they're a touch taller than the single-line departures rows (48), but only
	// just enough to fit the two lines — the extra height read as too much air.
	return 50;
#elif defined(PBL_ROUND)
	// Round rows stack the centered name over a centered badge + distance line, so
	// they need a little more height than the left-aligned rectangular rows.
	return 54;
#else
	return 48; // A bit taller than the Pencil design's 46 for breathing room
#endif
}

void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data)
{
	if (cell_index->section != 0) {
		return;
	}

	struct StopInfo *stop = &stops[cell_index->row];
	GRect bounds = layer_get_bounds(cell_layer);

#ifdef PBL_PLATFORM_EMERY
	{
	// Emery's larger screen gets a richer row: a mode icon on the left, the stop
	// name on top, and the distance below it.
	GColor name_color = GColorBlack;
	GColor row_bg = MENU_HL_COLOR;

	// Mode icon in its default look (black lines, white interior). Falls back to
	// no icon (text flush left) for unknown modes.
	int16_t icon_left = 8;
	int16_t text_x = icon_left;
	GDrawCommandImage *icon = stop->type[0] == 'B' ? busIcon
		: stop->type[0] == 'T' ? tramIcon : NULL;
	if (icon) {
		pdc_set_colors(icon, GColorBlack, GColorWhite);
		GSize isize = gdraw_command_image_get_bounds_size(icon);
		gdraw_command_image_draw(ctx, icon, GPoint(icon_left, (bounds.size.h - isize.h) / 2));
		text_x = icon_left + isize.w + 8;
	}

	int16_t text_w = bounds.size.w - text_x - 4;

	// Name on the upper half (scrolls when focused and overflowing), distance
	// below it. The two together are vertically centered in the row.
	int16_t cy = bounds.size.h / 2;
	marquee_draw_label(ctx, cell_layer, stop->name, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), name_color, GRect(text_x, cy - 21, text_w, 22), row_bg);

	char dist_e[12];
	snprintf(dist_e, sizeof(dist_e), "%d m", stop->dist);
	graphics_context_set_text_color(ctx, GColorDarkGray);
	graphics_draw_text(ctx, dist_e, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(text_x, cy - 2, text_w, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
	return;
	}
#endif

#ifdef PBL_ROUND
	{
	// Round display: everything is centered so nothing is clipped by the circular
	// bezel on the rows above and below the focused one. The stop name sits on top,
	// with the mode badge and distance centered together on the line below it.
	int16_t cy = bounds.size.h / 2;

	graphics_context_set_text_color(ctx, GColorBlack);
	graphics_draw_text(ctx, stop->name, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(6, cy - 22, bounds.size.w - 12, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

	char dist_r[12];
	snprintf(dist_r, sizeof(dist_r), "%d m", stop->dist);
	GFont dist_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
	GSize dsz = graphics_text_layout_get_content_size(dist_r, dist_font, GRect(0, 0, 160, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);

	const int16_t badge_size = 16;
	const int16_t gap = 5;
	bool has_badge = (stop->type[0] == 'B' || stop->type[0] == 'T');
	int16_t total = dsz.w + (has_badge ? badge_size + gap : 0);
	int16_t x = (bounds.size.w - total) / 2;
	int16_t by = cy + 3;

	if (has_badge) {
		GColor badge_color = stop->type[0] == 'B' ? GColorCobaltBlue : GColorRed;
		GRect badge = GRect(x, by, badge_size, badge_size);
		graphics_context_set_fill_color(ctx, badge_color);
		graphics_fill_rect(ctx, badge, 4, GCornersAll);
		char letter[2] = { stop->type[0], '\0' };
		graphics_context_set_text_color(ctx, GColorWhite);
		graphics_draw_text(ctx, letter, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GRect(x, by - 2, badge_size, badge_size + 2), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
		x += badge_size + gap;
	}

	graphics_context_set_text_color(ctx, GColorDarkGray);
	graphics_draw_text(ctx, dist_r, dist_font, GRect(x, by - 1, dsz.w + 4, 18), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
	return;
	}
#endif

	#ifdef PBL_COLOR
		GColor text_color = GColorBlack;
	#else
		GColor text_color = menu_cell_layer_is_highlighted(cell_layer) ? GColorWhite : GColorBlack;
	#endif
	GColor bg = COLOR_FALLBACK(MENU_HL_COLOR, GColorBlack);

	// Type badge on the left, vertically centered: a small rounded rectangle with a
	// white letter (B for bus, T for tram). The badge is drawn AFTER the name (in
	// the gutter the name is nudged right to clear) so the scrolling name slides
	// under it. On color watches it is colored by type and stays colored when
	// selected; on B&W watches the black badge inverts to a white badge with a
	// black letter on the solid-black selected row so it stays visible.
	int16_t cy = bounds.size.h / 2;
	int16_t text_x = 8;
	const int16_t badge_size = 18;
	bool has_badge = (stop->type[0] == 'B' || stop->type[0] == 'T');
	if (has_badge) {
		text_x = 6 + badge_size + 8;
	}

	int16_t text_w = bounds.size.w - text_x - 4;

	// Scrolls when this row is focused and the name overflows; otherwise static.
	marquee_draw_label(ctx, cell_layer, stop->name, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), text_color, GRect(text_x, cy - 21, text_w, 22), bg);

	if (has_badge) {
		#ifdef PBL_COLOR
			GColor badge_color = region_mode_color(stop->code, stop->type[0]);
			GColor letter_color = GColorWhite;
		#else
			bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
			GColor badge_color = highlighted ? GColorWhite : GColorBlack;
			GColor letter_color = highlighted ? GColorBlack : GColorWhite;
		#endif

		GRect badge = GRect(6, cy - badge_size / 2, badge_size, badge_size);
		graphics_context_set_fill_color(ctx, badge_color);
		graphics_fill_rect(ctx, badge, 4, GCornersAll);

		// Pebble draws glyphs near the top of their box, so nudge the centered
		// letter down to sit in the middle of the badge.
		char letter[2] = { stop->type[0], '\0' };
		graphics_context_set_text_color(ctx, letter_color);
		graphics_draw_text(ctx, letter, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GRect(badge.origin.x, badge.origin.y - 1, badge_size, badge_size), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
	}

	char dist[12];
	snprintf(dist, sizeof(dist), "%d m", stop->dist);
	graphics_context_set_text_color(ctx, text_color);
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
#ifdef PBL_ROUND
	// Leave room for the fixed "Nearby stops" bar pinned below the status bar.
	window_bounds.origin.y += ROUND_HEADER_HEIGHT;
	window_bounds.size.h -= ROUND_HEADER_HEIGHT;
#endif

	mainMenuLayer = menu_layer_create(window_bounds);
	// The round display pins its header in a fixed bar instead of a section header.
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
		.selection_changed = marquee_selection_changed,
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

#ifdef PBL_ROUND
	// Fixed "Nearby stops" header pinned below the status bar, above the loading
	// title so its opaque bar masks it. The menu was already inset below it.
	GRect hb = layer_get_bounds(window_layer);
	headerLayer = layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT, hb.size.w, ROUND_HEADER_HEIGHT));
	layer_set_update_proc(headerLayer, main_header_update_proc);
	layer_add_child(window_layer, headerLayer);
#endif

#ifdef PBL_PLATFORM_EMERY
	busIcon = gdraw_command_image_create_with_resource(RESOURCE_ID_IMAGE_BUS);
	tramIcon = gdraw_command_image_create_with_resource(RESOURCE_ID_IMAGE_TRAM);
#endif

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
#ifdef PBL_PLATFORM_EMERY
	if (busIcon) {
		gdraw_command_image_destroy(busIcon);
		busIcon = NULL;
	}
	if (tramIcon) {
		gdraw_command_image_destroy(tramIcon);
		tramIcon = NULL;
	}
#endif
}

void main_window_load(Window *window)
{
	// Clear any stops left over from a previous load so the menu does not render
	// stale rows (and its header) behind the loading overlay.
	stopAmount = 0;
	stops_loaded = false;
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
		// The overlay was just rebuilt with its default "Loading.." text; if the
		// earlier lookup finished with no stops, restore the empty-state message.
		if (stops_loaded && stopAmount == 0 && loadingLayer) {
			text_layer_set_text(loadingLayer, "No nearby stops");
		}
		main_set_loading(stopAmount == 0);
		// Restore the row the user left on, clamped in case the list shrank.
		if (stopAmount > 0) {
			uint16_t row = savedSelectedRow < (uint16_t)stopAmount ? savedSelectedRow : (uint16_t)(stopAmount - 1);
			menu_layer_set_selected_index(mainMenuLayer, MenuIndex(0, row), MenuRowAlignCenter, false);
		}
	}

	main_window_register_inbox();

	// Drive the scrolling stop names while this list is on screen.
	marquee_attach(mainMenuLayer);
}

// Free the layers whenever another window covers this one, returning their memory
// to the heap for the window on top.
void main_window_disappear(Window *window)
{
	marquee_detach(mainMenuLayer);
	main_destroy_ui();
}

void main_window_unload(Window *window)
{
	marquee_detach(mainMenuLayer);
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
