#include <pebble.h>
#include "home_window.h"
#include "main_window.h"
#include "pins.h"
#include "pins_window.h"
#include "splash_window.h" // BG_COLOR, shared with the splash screen

static Window *homeWindow;
static MenuLayer *homeMenuLayer;
static StatusBarLayer *statusLayer;

// The highlighted row is remembered across the menu layer being freed (while a
// sub-window is on top) and rebuilt, so going back to the home menu returns to the
// row the user left rather than resetting to the top.
static uint16_t savedSelectedRow = 0;

// PDC row icons, one per home item. Loaded on window load and reused for every
// redraw, mirroring how the error window holds its PDC icon.
static GDrawCommandImage *nearbyIcon;
static GDrawCommandImage *pinnedIcon;
static GBitmap *locationIcon; // small pin shown before the location label in the header

// Detected city, shown in the menu header. Resolved from the feed prefix of the
// nearest stop by the JS component (see getCityFromLocation in app.js). Persisted
// (key 50 — pins use 1 and 100+) so the last known city shows immediately on the
// next launch, before a fresh lookup completes. Empty means "unknown", in which
// case the header falls back to the app name.
#define PERSIST_KEY_CITY 50
static char detected_city[24] = "";

// True once the city lookup has replied (with a city or an empty "unknown"). It
// lets the header tell "still looking" apart from "looked, found nothing" so the
// "Loading.." placeholder is dropped instead of spinning forever in regions with
// no nearby stops, an unknown feed, or a GPS/network failure.
static bool city_resolved = false;

// True once the city request has actually been sent. The request is fired when
// the JS component signals it is ready (jsReady), with a fallback send when the
// home window loads in case that signal was missed; this guard keeps the two
// paths from sending it twice.
static bool city_requested = false;

// True once the JS component has signalled it is ready (jsReady). The city request
// must never be sent before this: a message sent while the JS side is still loading
// is dropped by the phone, yet it would still flip city_requested above and so
// suppress the real send once JS finally comes up — leaving the header stuck on
// "Loading.." forever. The emulator brings JS up almost instantly (before the
// splash times out), which is why this only ever bit on real hardware, where the
// Bluetooth JS bootstrap routinely takes longer than the splash.
static bool js_ready = false;

// Home menu items. Subtitle is optional; an empty subtitle renders a single
// vertically centered title (like "Nearby stops"). Icons will be added later.
struct HomeItem {
	char title[20];
	char subtitle[20];
};

#define NUM_HOME_ITEMS 2
#define HOME_ROW_NEARBY 0
#define HOME_ROW_PINNED 1

// Single-line header on the splash-screen background color: app name on the left
// edge, detected location on the right.
#define HOME_HEADER_HEIGHT 22

static struct HomeItem home_items[NUM_HOME_ITEMS] = {
	{ "Nearby stops", "" },
	{ "Pinned stops", "" }, // subtitle is filled in at draw time from the live count
};

uint16_t home_menu_get_num_sections_callback(MenuLayer *menu_layer, void *data)
{
	return 1;
}

uint16_t home_menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
	switch (section_index) {
		case 0:
			return NUM_HOME_ITEMS;
		default:
			return 0;
	}
}

int16_t home_menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
	return HOME_HEADER_HEIGHT;
}

void home_menu_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data)
{
	if (section_index != 0) {
		return;
	}
	GRect bounds = layer_get_bounds(cell_layer);

	// Splash-screen background, with the app name on the left edge and the detected
	// location on the right. Until the lookup resolves, "Loading.." holds its place;
	// once it resolves without a known city the label is dropped (leaving just the
	// app name) rather than showing "Loading.." forever.
	const char *location = NULL;
	if (detected_city[0] != '\0') {
		location = detected_city;
	} else if (!city_resolved) {
		location = "Loading..";
	}

	graphics_context_set_fill_color(ctx, COLOR_FALLBACK(BG_COLOR, GColorBlack));
	graphics_fill_rect(ctx, bounds, 0, GCornerNone);

	// Both texts share the band; each gets half the width so a long location cannot
	// run into the name. Text is white for contrast on the colored band.
	graphics_context_set_text_color(ctx, GColorWhite);
	int16_t pad = 4;
	int16_t half = bounds.size.w / 2;
	int16_t text_y = bounds.origin.y + 2;
	int16_t text_h = bounds.size.h - 2;

	graphics_draw_text(ctx, "Vuoro", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
		GRect(bounds.origin.x + pad, text_y, half - pad, text_h),
		GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

	// Nothing more to draw once the lookup has resolved without a known city: the
	// header is just the app name.
	if (!location) {
		return;
	}

	// Right side: the location label pinned to the right edge, preceded by a small
	// location pin. Measure the label first so the icon can sit immediately to its
	// left; the icon eats into the label's share of the band so the two never
	// collide with the app name on the left.
	GFont loc_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
	int16_t icon_w = locationIcon ? gbitmap_get_bounds(locationIcon).size.w : 0;
	int16_t icon_h = locationIcon ? gbitmap_get_bounds(locationIcon).size.h : 0;
	int16_t icon_gap = icon_w ? 3 : 0;
	int16_t right_edge = bounds.origin.x + bounds.size.w - pad;
	int16_t avail = half - pad - icon_w - icon_gap;
	GSize loc_sz = graphics_text_layout_get_content_size(location, loc_font,
		GRect(0, 0, avail, text_h), GTextOverflowModeTrailingEllipsis, GTextAlignmentRight);
	int16_t loc_w = loc_sz.w < avail ? loc_sz.w : avail;
	int16_t loc_x = right_edge - loc_w;

	graphics_draw_text(ctx, location, loc_font,
		GRect(loc_x, text_y, loc_w, text_h),
		GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

	if (locationIcon) {
		GRect icon_rect = GRect(loc_x - icon_gap - icon_w,
			bounds.origin.y + (bounds.size.h - icon_h) / 2, icon_w, icon_h);
		graphics_context_set_compositing_mode(ctx, GCompOpSet);
		graphics_draw_bitmap_in_rect(ctx, locationIcon, icon_rect);
	}
}

int16_t home_menu_get_cell_height_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	return 48; // Pebble-style menu row height, matching the stops list.
}

// Recolors every command in a PDC image. The row icons are line art (a black
// outline over a white interior); on B&W watches the interior flips to black on
// a selected (black) row so the glyph reads as a clean negative.
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

void home_menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data)
{
	if (cell_index->section != 0) {
		return;
	}

	struct HomeItem *item = &home_items[cell_index->row];
	GRect bounds = layer_get_bounds(cell_layer);

	// The Pinned stops subtitle reflects how many stops are currently pinned.
	char pin_subtitle[20];
	const char *subtitle = item->subtitle;
	if (cell_index->row == HOME_ROW_PINNED) {
		int count = pins_count();
		snprintf(pin_subtitle, sizeof(pin_subtitle), "%d stops", count);
		subtitle = pin_subtitle;
	}

#ifdef PBL_ROUND
	{
	// Round display: the icon + title are centered as a group (with the subtitle, if
	// any, centered below) so nothing is clipped by the circular bezel on the rows
	// above and below the focused one.
	GDrawCommandImage *icon = (cell_index->row == HOME_ROW_NEARBY) ? nearbyIcon : pinnedIcon;
	GFont title_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
	GSize tsz = graphics_text_layout_get_content_size(item->title, title_font, GRect(0, 0, bounds.size.w, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
	GSize isz = icon ? gdraw_command_image_get_bounds_size(icon) : GSize(0, 0);
	int16_t gap = icon ? 8 : 0;
	int16_t total = isz.w + gap + tsz.w;
	int16_t x = (bounds.size.w - total) / 2;
	bool has_sub = subtitle[0] != '\0';
	int16_t line_cy = has_sub ? bounds.size.h / 2 - 9 : bounds.size.h / 2;

	if (icon) {
		// Vertically center the icon in the whole row (against the title + subtitle
		// block), not just against the title line.
		gdraw_command_image_draw(ctx, icon, GPoint(x, (bounds.size.h - isz.h) / 2));
		x += isz.w + gap;
	}
	graphics_context_set_text_color(ctx, GColorBlack);
	graphics_draw_text(ctx, item->title, title_font, GRect(x, line_cy - 12, tsz.w + 6, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

	if (has_sub) {
		// Left-align the subtitle to the title's left edge (x) so it lines up under
		// the heading rather than being centered on the whole cell.
		graphics_context_set_text_color(ctx, GColorDarkGray);
		graphics_draw_text(ctx, subtitle, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(x, bounds.size.h / 2 + 5, bounds.size.w - x - 4, 18), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
	}
	return;
	}
#endif

	#ifdef PBL_COLOR
		GColor title_color = GColorBlack;
		GColor subtitle_color = GColorDarkGray;
	#else
		// B&W watches use the inverted black selection, so flip the text to white
		// on the highlighted row.
		GColor title_color = menu_cell_layer_is_highlighted(cell_layer) ? GColorWhite : GColorBlack;
		GColor subtitle_color = title_color;
	#endif

	// Row icon on the left, vertically centered like Pebble's own menu icons. The
	// title starts to the right of it. On B&W watches the black-outline glyph
	// inverts to a negative on the solid-black selected row so it stays visible.
	int16_t icon_left = 8;
	int16_t text_x = icon_left + 25 + 10; // fallback width if the PDC is missing
	GDrawCommandImage *icon = (cell_index->row == HOME_ROW_NEARBY) ? nearbyIcon : pinnedIcon;
	if (icon) {
		#ifndef PBL_COLOR
			bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
			pdc_set_colors(icon, highlighted ? GColorWhite : GColorBlack, highlighted ? GColorBlack : GColorWhite);
		#endif
		GSize icon_size = gdraw_command_image_get_bounds_size(icon);
		GPoint icon_origin = GPoint(icon_left, (bounds.size.h - icon_size.h) / 2);
		gdraw_command_image_draw(ctx, icon, icon_origin);
		text_x = icon_left + icon_size.w + 10;
	}

	int16_t text_w = bounds.size.w - text_x - 4;
	int16_t cy = bounds.size.h / 2;

	graphics_context_set_text_color(ctx, title_color);

	if (subtitle[0] != '\0') {
		// Title above the midline, subtitle below it.
		graphics_draw_text(ctx, item->title, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(text_x, cy - 21, text_w, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
		graphics_context_set_text_color(ctx, subtitle_color);
		graphics_draw_text(ctx, subtitle, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(text_x, cy + 1, text_w, 18), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
	} else {
		// No subtitle: vertically center the title.
		graphics_draw_text(ctx, item->title, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(text_x, cy - 13, text_w, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
	}
}

void home_menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	switch (cell_index->row) {
		case HOME_ROW_NEARBY:
			window_stack_push(main_window_get_window(), true);
			break;
		case HOME_ROW_PINNED:
			pins_window_show();
			break;
		default:
			break;
	}
}

void home_setup_menu_layer(Window *window, Layer *window_layer)
{
	GRect window_bounds = layer_get_bounds(window_layer);
	window_bounds.origin.y = STATUS_BAR_LAYER_HEIGHT;
#ifdef PBL_ROUND
	// The round display normally keeps the focused row centered, which slides the
	// whole menu when the selection moves. With only two items, size the menu to
	// exactly fit them and center that block vertically, so the rows stay put and
	// only the highlight moves (see menu_layer_set_center_focused below).
	int16_t full_h = window_bounds.size.h;
	int16_t content_h = NUM_HOME_ITEMS * home_menu_get_cell_height_callback(NULL, NULL, NULL);
	// Start the menu so the two-row block is vertically centered, and leave slack
	// below it (rather than sizing the frame to the content exactly) so the rows
	// top-align in the frame and hold still instead of re-centering on selection.
	window_bounds.origin.y = (full_h - content_h) / 2;
	window_bounds.size.h = full_h - window_bounds.origin.y;
#endif

	homeMenuLayer = menu_layer_create(window_bounds);
	// Pebble Round has no room for a section header, so it is left off there.
	menu_layer_set_callbacks(homeMenuLayer, NULL, (MenuLayerCallbacks){
		#ifdef PBL_RECT
		 .get_num_sections = home_menu_get_num_sections_callback,
		 .get_header_height = home_menu_get_header_height_callback,
		 .draw_header = home_menu_draw_header_callback,
		#endif
		.get_num_rows = home_menu_get_num_rows_callback,
		.get_cell_height = home_menu_get_cell_height_callback,
		.draw_row = home_menu_draw_row_callback,
		.select_click = home_menu_select_callback,
	});

#ifdef PBL_ROUND
	// Lay the rows out top-down (no recentering on selection) so they hold still.
	menu_layer_set_center_focused(homeMenuLayer, false);
#endif

	// Color watches use the light-gray selection (black row icons stay visible);
	// B&W watches use the classic inverted black selection, where the row icons and
	// text flip to white when highlighted.
	menu_layer_set_highlight_colors(homeMenuLayer, COLOR_FALLBACK(HOME_HL_COLOR, GColorBlack), COLOR_FALLBACK(GColorBlack, GColorWhite));
	menu_layer_set_click_config_onto_window(homeMenuLayer, window);

	layer_add_child(window_layer, menu_layer_get_layer(homeMenuLayer));
}

// Builds the home menu, its row icons and the status bar. Split out so it can run
// both on initial load and when the window is revealed again after being covered
// (see home_window_appear/disappear). No-op if already built.
static void home_build_ui(Window *window)
{
	if (homeMenuLayer) {
		return;
	}
	Layer *window_layer = window_get_root_layer(window);

	home_setup_menu_layer(window, window_layer);

	nearbyIcon = gdraw_command_image_create_with_resource(RESOURCE_ID_IMAGE_STOP);
	pinnedIcon = gdraw_command_image_create_with_resource(RESOURCE_ID_IMAGE_TIMELINE_PIN);
	locationIcon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_LOCATION);

	statusLayer = status_bar_layer_create();
	status_bar_layer_set_separator_mode(statusLayer, StatusBarLayerSeparatorModeDotted);
	status_bar_layer_set_colors(statusLayer, GColorClear, GColorBlack);
	layer_add_child(window_layer, status_bar_layer_get_layer(statusLayer));
}

// Frees the home menu, its icons and status bar. The selected row is remembered
// first (see savedSelectedRow) so it can be restored when the menu is rebuilt;
// freeing the rest whenever another window is on top keeps aplite's small heap
// available for that window.
static void home_destroy_ui(void)
{
	if (homeMenuLayer) {
		savedSelectedRow = menu_layer_get_selected_index(homeMenuLayer).row;
		menu_layer_destroy(homeMenuLayer);
		homeMenuLayer = NULL;
	}
	if (statusLayer) {
		status_bar_layer_destroy(statusLayer);
		statusLayer = NULL;
	}
	if (nearbyIcon) {
		gdraw_command_image_destroy(nearbyIcon);
		nearbyIcon = NULL;
	}
	if (pinnedIcon) {
		gdraw_command_image_destroy(pinnedIcon);
		pinnedIcon = NULL;
	}
	if (locationIcon) {
		gbitmap_destroy(locationIcon);
		locationIcon = NULL;
	}
}

static void home_request_city(void);

// Handles the JS replies that drive the menu header: jsReady (fire the request),
// cityUnknown (lookup failed — keep the last known city), and cityName (the
// authoritative result — show it, or clear the header if it came back empty). A
// late response is fine: the menu reloads if it is still built.
static void home_message_inbox(DictionaryIterator *iter, void *context)
{
	// The JS component just came up. This is the earliest point it can serve a
	// request without the message being dropped, so remember that JS is ready and
	// fire the startup city lookup now (unless it already went out).
	if (dict_find(iter, MESSAGE_KEY_jsReady)) {
		js_ready = true;
		if (!city_requested) {
			home_request_city();
		}
		return;
	}

	// The lookup could not run (no network, timed out, GPS unavailable, bad
	// response). We genuinely don't know the city, so keep the last known one
	// rather than blanking it over a transient failure — but mark it resolved so
	// the header stops showing "Loading.." (it falls back to just the app name if
	// there was no last known city).
	if (dict_find(iter, MESSAGE_KEY_cityUnknown)) {
		APP_LOG(APP_LOG_LEVEL_INFO, "HomeWindow: City lookup could not determine a city.");
		city_resolved = true;
		if (homeMenuLayer) {
			menu_layer_reload_data(homeMenuLayer);
		}
		return;
	}

	Tuple *city = dict_find(iter, MESSAGE_KEY_cityName);
	if (!city) {
		return;
	}
	// A cityName reply means the lookup completed, so the header should stop showing
	// "Loading..". Unlike cityUnknown above, this is an authoritative answer: an
	// empty cstring (length 1: just the terminator) means the lookup ran and found
	// no known city here, so clear the stale last known city (and its persisted
	// copy) to keep the header up to date rather than clinging to a previous one.
	city_resolved = true;
	if (city->length > 1) {
		strncpy(detected_city, city->value->cstring, sizeof(detected_city) - 1);
		detected_city[sizeof(detected_city) - 1] = '\0';
		persist_write_string(PERSIST_KEY_CITY, detected_city);
	} else {
		APP_LOG(APP_LOG_LEVEL_INFO, "HomeWindow: City lookup found no known city here.");
		detected_city[0] = '\0';
		persist_delete(PERSIST_KEY_CITY);
	}
	if (homeMenuLayer) {
		menu_layer_reload_data(homeMenuLayer);
	}
}

// Asks the JS component to resolve the surrounding city. The reply arrives as a
// cityName message handled by home_message_inbox.
static void home_request_city(void)
{
	DictionaryIterator *iter;
	if (app_message_outbox_begin(&iter) != APP_MSG_OK || iter == NULL) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "HomeWindow: could not begin city request.");
		return;
	}
	dict_write_uint16(iter, MESSAGE_KEY_cityMessage, 1);
#ifdef SCREENSHOT_MODE
	// Tell the JS side to serve fixtures. JS latches this on first sight, so a
	// single flag suffices: the city request is the first message the watch sends
	// (fired at startup, during the splash), well before any data screen opens.
	dict_write_uint8(iter, MESSAGE_KEY_screenshotMode, 1);
#endif
	dict_write_end(iter);
	app_message_outbox_send();
	city_requested = true;
}

// Prepares the city lookup before the home window exists (called from init while
// the splash is up). It loads the last known city and registers the inbox handler
// so it can catch the JS component's jsReady signal, which is what actually fires
// the request — sending it from here would be too early (the JS side is not
// loaded yet) and the message would be dropped. Doing this during the splash gives
// the reply time to arrive before the menu paints, so the header is usually
// already final instead of swapping from "Loading.." to the city a moment later.
void home_window_start_location_lookup()
{
	// Show the last known city straight away; the fresh lookup updates it.
	if (persist_exists(PERSIST_KEY_CITY)) {
		persist_read_string(PERSIST_KEY_CITY, detected_city, sizeof(detected_city));
	}
	// A fresh lookup is pending; the header shows "Loading.." until it replies
	// (unless a last known city is already on screen).
	city_resolved = false;
	app_message_register_inbox_received(home_message_inbox);
}

void home_window_load(Window *window)
{
	// The city lookup was prepared during the splash (see
	// home_window_start_location_lookup); detected_city/city_resolved hold its
	// result-so-far, so the menu paints with the right header straight away.
	home_build_ui(window);
	app_message_register_inbox_received(home_message_inbox);
	// Fallback: only if JS is already up but the request has not gone out (e.g. a
	// failed outbox_begin) do we send it now. We must NOT send before jsReady: on
	// real hardware the splash often times out and this load runs while the JS side
	// is still booting, so sending here would be dropped by the phone and would burn
	// the city_requested guard, blocking the real send the jsReady handler makes a
	// moment later. While JS is not ready we leave the request to that handler.
	if (js_ready && !city_requested) {
		home_request_city();
	}
}

// Rebuild the UI if it was freed while another window was on top, and redraw so
// the Pinned stops count is up to date after pinning/unpinning elsewhere.
void home_window_appear(Window *window)
{
	home_build_ui(window);
	// Reclaim the AppMessage inbox from whichever window was on top, so a city
	// reply (e.g. one still in flight) is handled here.
	app_message_register_inbox_received(home_message_inbox);
	// If we are back here still not knowing the city, the reply may have landed on a
	// sub-window's inbox handler while we were away (each data window swaps in its
	// own) and been lost. Now that our handler is active again, ask once more so the
	// header can still fill in. Skipped until JS is up (the jsReady handler owns the
	// first request) and once we have a resolved answer, so it never loops.
	if (js_ready && !city_resolved) {
		home_request_city();
	}
	if (homeMenuLayer) {
		menu_layer_reload_data(homeMenuLayer);
		// Restore the row the user left on before the menu was freed.
		menu_layer_set_selected_index(homeMenuLayer, MenuIndex(0, savedSelectedRow), MenuRowAlignCenter, false);
	}
}

// Free the UI whenever another window covers the home menu, returning its memory
// to the heap for the window on top.
void home_window_disappear(Window *window)
{
	home_destroy_ui();
}

void home_window_unload(Window *window)
{
	home_destroy_ui();
}

void home_window_create()
{
	homeWindow = window_create();
	window_set_window_handlers(homeWindow, (WindowHandlers) {
		.load = home_window_load,
		.appear = home_window_appear,
		.disappear = home_window_disappear,
		.unload = home_window_unload
	});
}

void home_window_destroy()
{
	window_destroy(homeWindow);
}

Window *home_window_get_window()
{
	return homeWindow;
}
