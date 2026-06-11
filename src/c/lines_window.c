#include <pebble.h>
#include "lines_window.h"
#include "main_window.h"
#include "error_window.h"
#include "pins.h"
#include "marquee.h"
#include "region.h"

// The "Show later" paging UI is left out of aplite: its ~24KB heap is already
// tight for the departures view (which is why this window frees its layers when
// covered), so aplite keeps the original single-window list. On every other
// platform SHOW_LATER_ENABLED turns on the extra row and its handling.
#ifndef PBL_PLATFORM_APLITE
#define SHOW_LATER_ENABLED
#endif

static Window *linesWindow;
static MenuLayer *lineMenuLayer;
static StatusBarLayer *statusLayer;
// Draws the fare-zone badge over the top-right corner of the status bar. Sits
// above statusLayer so the circle covers the bar rather than being painted over.
static Layer *zoneLayer;
static TextLayer *loadingLayer;
static TextLayer *titleLayer;

static char stopCode[20];
char stopName[30];
// The stop's vehicle mode ('B', 'T' or '\0' unknown), used to color the header
// bar and its type badge, and to tag a pin created from this screen.
static char stopType;
// The stop's fare zone (e.g. "A"), shown as a badge on the status bar. Empty
// when the stop has no zone or none was reported. Cleared on each fresh load so
// a stop without a zone does not inherit the previous stop's badge.
static char stopZone[8];
// The stop's public code (e.g. "H1224"), shown as a badge in the emery header.
// Arrives as a stop-level message with the departures; cleared on each load so a
// stop without a code does not inherit the previous one's. Only rendered on emery.
static char stopShortCode[12];
static int lineAmount = 0;
static struct LineInfo lines[NUM_LINES];

#if defined(PBL_PLATFORM_EMERY) || defined(PBL_ROUND)
// Mode icon (bus/tram line art) drawn in the colored header bar. Loaded with the
// window's other layers. Emery and the round display both show it in the header.
static GDrawCommandImage *busIcon;
static GDrawCommandImage *tramIcon;

// Recolors every command in a PDC image, used to tint the line-art mode icon
// white for the colored header tile. Mirrors home_window.c's helper.
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

// Remembers the highlighted row while the menu layer is freed (e.g. with the
// action-menu or feedback window on top) so returning to the departures list
// restores the selected row rather than resetting to the top.
static uint16_t savedSelectedRow = 0;

// Which window of departures to fetch. LOAD is the stop's first ("now") window,
// REFRESH re-fetches the window currently shown (the once-a-minute update), and
// MORE advances to the next window for the "Show later" row.
typedef enum {
	LINE_REQ_LOAD,
	LINE_REQ_REFRESH,
	LINE_REQ_MORE,
} LineRequestType;

#ifdef SHOW_LATER_ENABLED
// While a "Show later" fetch is in flight the menu is blanked; this remembers the
// count to restore if that fetch turns up no further departures. The timer shows
// the "No later departures" notice for a couple of seconds before restoring.
static int prev_line_amount;
static AppTimer *no_more_timer;
#endif

static void lines_request_departures(LineRequestType type);

static bool line_transfer_started;
// Set while the once-a-minute auto refresh is in flight, so the loading
// indicator and the completion vibration are suppressed and a failed refresh
// keeps the last departures on screen instead of erroring out.
static bool line_silent_refresh;
static bool line_got_name;
static bool line_got_time;
static bool line_got_dir;
static int line_index;

void lines_window_show(char *code, char *name, char *type)
{
	strncpy(stopCode, code, sizeof(stopCode));
	strncpy(stopName, name, sizeof(stopName));
	stopType = type[0];
	window_stack_push(lines_window_get_window(), true);
}

// Shows the title + centered "Loading.." text while waiting for data, or hides
// them to reveal the populated departures menu (whose own header takes over). The
// title bar is not created on round (it only renders its header once data lands),
// so each layer is toggled independently.
static void lines_set_loading(bool loading)
{
	// The overlay layers only exist while the window is built (see lines_build_ui).
	if (loadingLayer) {
		layer_set_hidden(text_layer_get_layer(loadingLayer), !loading);
	}
	if (titleLayer) {
		layer_set_hidden(text_layer_get_layer(titleLayer), !loading);
	}
}

#ifdef SHOW_LATER_ENABLED
// Fired ~2s after a "Show later" fetch reports no further departures: restores
// the departures that were on screen before the fetch (still untouched in
// `lines`, since the empty response sent no items) and parks the selection back
// on the "Show later" row so the user can try again or scroll up.
static void no_more_restore(void *data)
{
	no_more_timer = NULL;
	lineAmount = prev_line_amount;
	if (loadingLayer) {
		text_layer_set_text(loadingLayer, "Loading..");
	}
	lines_set_loading(false);
	if (lineMenuLayer) {
		// The menu was hidden when "Show later" was pressed; reveal it again with
		// the restored departures and park the selection on the "Show later" row.
		layer_set_hidden(menu_layer_get_layer(lineMenuLayer), false);
		menu_layer_reload_data(lineMenuLayer);
		menu_layer_set_selected_index(lineMenuLayer, MenuIndex(0, lineAmount), MenuRowAlignCenter, false);
	}
}

// Shows "No later departures" where the "Loading.." text was, then schedules the
// previous list to come back. Used when a "Show later" fetch finds nothing more.
static void lines_show_no_more(void)
{
	if (loadingLayer) {
		text_layer_set_text(loadingLayer, "No later departures");
	}
	lines_set_loading(true);
	if (no_more_timer) {
		app_timer_cancel(no_more_timer);
	}
	no_more_timer = app_timer_register(3000, no_more_restore, NULL);
}
#endif // SHOW_LATER_ENABLED

// Maps a Digitransit vehicle mode string to a single-letter type indicator.
// Unknown modes produce an empty string so nothing is shown.
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

static void process_line_tuple(Tuple *t)
{
	uint16_t key = t->key;

	if (key == MESSAGE_KEY_lineCode) {
		strncpy(lines[line_index].code, t->value->cstring, 10);
		line_got_name = true;
	}
	else if (key == MESSAGE_KEY_lineTime) {
		strncpy(lines[line_index].time, t->value->cstring, 10);
		line_got_time = true;
	}
	else if (key == MESSAGE_KEY_lineDir) {
		strncpy(lines[line_index].dir, t->value->cstring, 30);
		line_got_dir = true;
	}
	else if (key == MESSAGE_KEY_lineMode) {
		// Optional field, so it does not gate completion of a line.
		mode_to_type_letter(t->value->cstring, lines[line_index].type);
	}
	else if (key == MESSAGE_KEY_lineRealtime) {
		// Optional field, so it does not gate completion of a line.
		lines[line_index].realtime = (t->value->int32 != 0);
	}
	else if (key == MESSAGE_KEY_lineMins) {
		// Optional field, so it does not gate completion of a line.
		lines[line_index].mins = t->value->int32;
	}
	else if (key == MESSAGE_KEY_stopZone) {
		// Stop-level value sent ahead of the departures; store it and repaint the
		// badge. Not tied to a line, so it does not touch lines[line_index].
		strncpy(stopZone, t->value->cstring, sizeof(stopZone) - 1);
		stopZone[sizeof(stopZone) - 1] = '\0';
		if (zoneLayer) {
			layer_mark_dirty(zoneLayer);
		}
	}
	else if (key == MESSAGE_KEY_stopShortCode) {
		// Stop-level value sent ahead of the departures (like the zone); stored
		// for the emery header badge. Not tied to a line.
		strncpy(stopShortCode, t->value->cstring, sizeof(stopShortCode) - 1);
		stopShortCode[sizeof(stopShortCode) - 1] = '\0';
	}
	else if (key == MESSAGE_KEY_lineMessage) {
		return;
	}
	else {
		APP_LOG(APP_LOG_LEVEL_ERROR, "LinesWindow: AppMessage contained obscure key!");
	}
}

void lines_message_inbox(DictionaryIterator *iter, void *context)
{
	if (dict_find(iter, MESSAGE_KEY_noInternet)) {
		APP_LOG(APP_LOG_LEVEL_WARNING, "JS reported no internet connection!");
		line_transfer_started = false;
		// A failed background refresh keeps the last departures on screen rather
		// than covering them with an error; the next minute's refresh tries again.
		if (line_silent_refresh) {
			return;
		}
		error_window_set_error("No internet connection", ERROR_ICON_NO_INTERNET);
		error_window_show();
		return;
	}

#ifdef SHOW_LATER_ENABLED
	if (dict_find(iter, MESSAGE_KEY_lineNoMore)) {
		// The "Show later" fetch found no further departures. Briefly say so, then
		// restore the list we had; `lines` was never overwritten, so it survives.
		APP_LOG(APP_LOG_LEVEL_INFO, "LinesWindow: No later departures available.");
		line_transfer_started = false;
		lines_show_no_more();
		return;
	}
#endif

	Tuple *t = dict_find(iter, MESSAGE_KEY_lineMessage);
	if (t) {
		if (!line_transfer_started) {
			APP_LOG(APP_LOG_LEVEL_INFO, "LinesWindow: Started lines message transfer.");
			line_transfer_started = true;
			line_got_name = false;
			line_got_time = false;
			line_got_dir = false;
			line_index = 0;
		}
		// Clear the optional fields for the current line, so a line that omits an
		// optional key does not keep a stale value from a previous transfer.
		lines[line_index].type[0] = '\0';
		lines[line_index].realtime = false;
		lines[line_index].mins = -1;
	}
	else {
		t = dict_find(iter, MESSAGE_KEY_messageEnd);
		if (t) {
			APP_LOG(APP_LOG_LEVEL_INFO, "LinesWindow: Line message transfer ended. Total of %d lines were transfered.", line_index);
			line_transfer_started = false;
			lineAmount = line_index;
			// The list may have been freed if the window was covered before the data
			// arrived; the count is retained so it renders when the window reappears.
			if (lineMenuLayer) {
				menu_layer_reload_data(lineMenuLayer);
			}
			// On the once-a-minute refresh the times just change in place: no
			// loading indicator was shown and no vibration is wanted. A foreground
			// load (initial or "Show later") reveals the list and confirms with a
			// short pulse. The "Show later" path hid the menu while loading, so
			// unhide it here; it already sits at the top (with its header, first row
			// selected) from being rebuilt out of the empty state.
			if (!line_silent_refresh) {
				lines_set_loading(false);
#ifdef SHOW_LATER_ENABLED
				if (lineMenuLayer) {
					layer_set_hidden(menu_layer_get_layer(lineMenuLayer), false);
					// Put focus on the first departure. MenuRowAlignNone selects it
					// without scrolling, so the header stays visible (the menu is
					// already at the top from the empty-state reset).
					menu_layer_set_selected_index(lineMenuLayer, MenuIndex(0, 0), MenuRowAlignNone, false);
				}
#endif
				vibes_short_pulse();
			}
			return;
		}
	}

	t = dict_read_first(iter);
	if (t) {
		if (t->key == MESSAGE_KEY_lineNoFound) {
			APP_LOG(APP_LOG_LEVEL_WARNING, "JS component was not able to find departing lines!");
			// Stay on the lines screen (empty menu) and replace the loading
			// indicator with a "No departures" message instead of erroring out.
			line_transfer_started = false;
			// On a background refresh keep the departures already shown rather than
			// wiping them; only show "No departures" on an explicit load.
			if (!line_silent_refresh && loadingLayer) {
				text_layer_set_text(loadingLayer, "No departures");
			}
			return;
		}
		process_line_tuple(t);
	}
	while (t != NULL) {
		t = dict_read_next(iter);
		if (t) {
			process_line_tuple(t);
		}
	}

	if (line_got_name && line_got_time && line_got_dir) {
		++line_index;
		line_got_name = false;
		line_got_time = false;
		line_got_dir = false;
	}
}

uint16_t lines_get_num_sections_callback(MenuLayer *menu_layer, void *data)
{
	return 1;
}

uint16_t lines_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
	switch (section_index) {
		case 0:
#ifdef SHOW_LATER_ENABLED
			// One extra row for the "Show later" action, but only once there are
			// departures to extend (not while loading or when the stop is empty).
			return lineAmount > 0 ? lineAmount + 1 : 0;
#else
			return lineAmount;
#endif
		default:
			return 0;
	}
}

#ifdef SHOW_LATER_ENABLED
// True for the synthetic "Show later" row, which sits just past the departures.
static bool is_show_later_row(const MenuIndex *cell_index)
{
	return cell_index->section == 0 && cell_index->row == lineAmount;
}
#endif

int16_t lines_get_cell_height_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	if(cell_index->section == 0) {
#ifdef SHOW_LATER_ENABLED
		// The "Show later" action is a plain single-line row.
		if (is_show_later_row(cell_index)) {
			return 44;
		}
#endif
		#if defined(PBL_ROUND)
			return 78; // Badge, destination, then the countdown + time on one line
		#elif defined(PBL_PLATFORM_EMERY)
			return 48; // Single-line row: badge + destination, time/countdown at right
		#else
			return 60; // Tall enough to pad the badge and destination top/bottom
		#endif
	}
	else return 44; //Default height of cell
}

// Emery's header is a taller colored bar carrying a mode-icon tile, the stop name
// and the stop-code badge; the other platforms keep the compact title bar.
#if defined(PBL_PLATFORM_EMERY)
#define LINES_HEADER_HEIGHT 36
#elif defined(PBL_ROUND)
#define LINES_HEADER_HEIGHT 48
#else
#define LINES_HEADER_HEIGHT MENU_CELL_BASIC_HEADER_HEIGHT
#endif

int16_t lines_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
	// No header until the departures arrive. On round the menu draws its section
	// header even while the section is empty, which would put the stop-name bar over
	// the "Loading.." overlay; collapsing the header to zero height keeps it hidden
	// until there is data (and the stop's mode/color) to show.
	if (lineAmount == 0) {
		return 0;
	}
	return LINES_HEADER_HEIGHT;
}

void lines_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data)
{
	if (section_index != 0 || lineAmount == 0) {
		return;
	}

	GRect bounds = layer_get_bounds(cell_layer);
	GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);

#ifdef PBL_ROUND
	// Round display gets an emery-style header: a colored bar (blue bus / red tram)
	// with a white mode icon centered above the stop name, also centered, in white.
	// An unknown mode keeps a plain white bar with black text and no icon, as the
	// color carries no meaning there. Stacking the icon above the name (rather than
	// beside it) lets the name use the bar's full width.
	{
	bool colored = (stopType == 'B' || stopType == 'T');
	GColor fg = colored ? GColorWhite : GColorBlack;
	GColor bar = colored ? (stopType == 'B' ? GColorCobaltBlue : GColorRed) : GColorWhite;
	graphics_context_set_fill_color(ctx, bar);
	graphics_fill_rect(ctx, bounds, 0, GCornerNone);

	int16_t y = bounds.origin.y + 4;
	GDrawCommandImage *hicon = stopType == 'B' ? busIcon : stopType == 'T' ? tramIcon : NULL;
	if (hicon) {
		// The icon keeps its default look (black lines, white interior); it reads on
		// either colored bar.
		pdc_set_colors(hicon, GColorBlack, GColorWhite);
		GSize hs = gdraw_command_image_get_bounds_size(hicon);
		gdraw_command_image_draw(ctx, hicon, GPoint((bounds.size.w - hs.w) / 2, y));
		y += hs.h;
	}

	// Stop name centered below the icon, using the full bar width.
	graphics_context_set_text_color(ctx, fg);
	graphics_draw_text(ctx, (char *)stopName, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(8, y - 3, bounds.size.w - 16, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
	return;
	}
#endif

#ifdef PBL_PLATFORM_EMERY
	// Emery gets a richer header: a colored bar (blue bus / red tram) with a white
	// mode icon, the stop name and the public stop-code badge. An unknown mode
	// keeps a plain white bar with black text, as the color carries no meaning.
	// One row: mode icon, stop name, and the stop-code badge aligned to the right
	// edge, all vertically centered in the bar. An unknown mode keeps a plain white
	// bar with black text.
	bool colored = (stopType == 'B' || stopType == 'T');
	GColor fg = colored ? GColorWhite : GColorBlack;
	GColor bar = colored ? region_mode_color(stopCode, stopType) : GColorWhite;
	graphics_context_set_fill_color(ctx, bar);
	graphics_fill_rect(ctx, bounds, 0, GCornerNone);

	// Icon geometry, resolved up front so the name's left edge clears it. The icon
	// itself is drawn after the name so the scrolling name slides under it.
	int16_t hx = 5;
	GDrawCommandImage *hicon = stopType == 'B' ? busIcon : stopType == 'T' ? tramIcon : NULL;
	GSize hs = GSize(0, 0);
	if (hicon) {
		hs = gdraw_command_image_get_bounds_size(hicon);
		hx = 5 + hs.w + 6;
	}

	// Badge geometry, resolved up front for the same reason. The name fills the
	// space between the icon and the badge (or the right margin when there is no
	// code).
	bool has_code = stopShortCode[0] != '\0';
	GFont badge_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
	int16_t bw = 0, bx = 0, bh = 18, by = (bounds.size.h - 18) / 2;
	int16_t name_right = bounds.size.w - 5;
	if (has_code) {
		GSize ct = graphics_text_layout_get_content_size(stopShortCode, badge_font, GRect(0, 0, 90, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft);
		bw = ct.w + 10;
		bx = bounds.size.w - bw - 5;
		name_right = bx - 6;
	}

	// Stop name, auto-scrolling since the header cannot be focused. Both gutters
	// are masked with the bar color so the scrolled text vanishes before the icon
	// and the badge, which are painted over those gutters next.
	marquee_draw_auto_label(ctx, (char *)stopName, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), fg, GRect(hx, 6, name_right - hx, 24), bar, 0, bounds.size.w);

	if (hicon) {
		// The icon keeps its default look (black lines, white interior) on the bar.
		pdc_set_colors(hicon, GColorBlack, GColorWhite);
		gdraw_command_image_draw(ctx, hicon, GPoint(5, (bounds.size.h - hs.h) / 2));
	}
	if (has_code) {
		GRect badge = GRect(bx, by, bw, bh);
		// Thin outlined pill in the foreground color so it reads on either bar
		// color. Stroke width is pinned to 1 so the border stays hairline.
		graphics_context_set_stroke_width(ctx, 1);
		graphics_context_set_stroke_color(ctx, fg);
		graphics_draw_round_rect(ctx, badge, 3);
		graphics_context_set_text_color(ctx, fg);
		graphics_draw_text(ctx, stopShortCode, badge_font, GRect(bx + 5, by, bw, bh + 4), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
	}
	return;
#endif

	// On color watches a known type tints the title bar (blue for bus, red for
	// tram) with the stop name in white. B&W watches keep a plain white header with
	// black text, since the color carries no meaning there.
	GColor text_color = GColorBlack;
#ifdef PBL_COLOR
	if (stopType == 'B' || stopType == 'T') {
		graphics_context_set_fill_color(ctx, region_mode_color(stopCode, stopType));
		graphics_fill_rect(ctx, bounds, 0, GCornerNone);
		text_color = GColorWhite;
	}
#endif

	graphics_context_set_text_color(ctx, text_color);
	graphics_draw_text(ctx, (char *)stopName, font, bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// Picks the badge background color for a line type. On color watches buses are
// blue and trams red; on black-and-white watches both fall back to black, where
// the type is not distinguished.
static GColor badge_color_for_type(const char *type)
{
	// region_mode_color keys off the viewed stop's feed (HSL trams are green); the
	// fallback keeps both modes black on black-and-white watches.
	return COLOR_FALLBACK(region_mode_color(stopCode, type[0]), GColorBlack);
}

#ifdef PBL_ROUND
static int16_t code_badge_width(GContext *ctx, const char *code, GFont font)
{
	GSize text = graphics_text_layout_get_content_size(code, font, GRect(0, 0, 90, 40), GTextOverflowModeWordWrap, GTextAlignmentLeft);
	return text.w + 10;
}
#endif

// Draws the line code inside a rounded, type-colored badge at (x, y). On color
// watches the badge keeps its type color even when focused, since the light-gray
// selection leaves it legible. On B&W watches the badge is black, so on the
// solid-black focused row it inverts to a white pill with the line code in black
// to stay visible.
static void draw_code_badge(GContext *ctx, const char *code, const char *type, int16_t x, int16_t y, GFont font, bool highlighted)
{
	GSize text = graphics_text_layout_get_content_size(code, font, GRect(0, 0, 90, 40), GTextOverflowModeWordWrap, GTextAlignmentLeft);
	int16_t bw = text.w + 12;
	int16_t bh = text.h + 2;

	GColor type_color = badge_color_for_type(type);
	#ifdef PBL_COLOR
		(void)highlighted;
		GColor badge_bg = type_color;
		GColor badge_text = GColorWhite;
	#else
		GColor badge_bg = highlighted ? GColorWhite : type_color;
		GColor badge_text = highlighted ? type_color : GColorWhite;
	#endif

	graphics_context_set_fill_color(ctx, badge_bg);
	graphics_fill_rect(ctx, GRect(x, y, bw, bh), 4, GCornersAll);

	graphics_context_set_text_color(ctx, badge_text);
	// Pebble draws the glyph near the top of its box with descender space below,
	// so a number (no descenders) looks off-center. Nudge it to center it vertically.
	graphics_draw_text(ctx, code, font, GRect(x + 6, y - 3, bw, bh + 4), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

void lines_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data)
{
	if (cell_index->section != 0) {
		return;
	}

#ifdef SHOW_LATER_ENABLED
	// The synthetic last row is the "Show later" action, not a departure.
	if (is_show_later_row(cell_index)) {
		#ifdef PBL_COLOR
			GColor action_color = GColorBlack;
		#else
			GColor action_color = menu_cell_layer_is_highlighted(cell_layer) ? GColorWhite : GColorBlack;
		#endif
		GRect bounds = layer_get_bounds(cell_layer);
		// Nudge the single line down so it sits centered in the 44px row.
		bounds.origin.y += (bounds.size.h - 22) / 2;
		graphics_context_set_text_color(ctx, action_color);
		graphics_draw_text(ctx, "Show later", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
		return;
	}
#endif

	struct LineInfo *line = &lines[cell_index->row];
	// On color watches the selection is light gray, so text stays black. On
	// black-and-white watches the selection is black, so highlighted text is white.
	#ifdef PBL_COLOR
		GColor text_color = GColorBlack;
	#else
		GColor text_color = menu_cell_layer_is_highlighted(cell_layer) ? GColorWhite : GColorBlack;
	#endif

	// Live realtime predictions are shown in green; scheduled times use the
	// normal text color. On black-and-white watches green falls back to normal.
	// The highlighted row has a light-gray background, where a medium green is
	// hard to read, so use a darker green there and the brighter green elsewhere.
	GColor live_green = menu_cell_layer_is_highlighted(cell_layer) ? GColorDarkGreen : GColorIslamicGreen;
	GColor time_color = line->realtime ? COLOR_FALLBACK(live_green, text_color) : text_color;

	// "X min" countdown, shown only when the departure is less than 10 minutes
	// away, regardless of whether realtime info is available.
	char mins_buf[12];
	bool show_mins = (line->mins >= 0 && line->mins < 10);
	if (show_mins) {
		if (line->mins == 0) {
			// Departing this minute: "now" reads better than "0 min".
			snprintf(mins_buf, sizeof(mins_buf), "now");
		} else {
			snprintf(mins_buf, sizeof(mins_buf), "%d min", line->mins);
		}
	}

	// Take Round and Pebble 2 into account
	#ifdef PBL_ROUND
		GFont code_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
		int16_t bw = code_badge_width(ctx, line->code, code_font);
		draw_code_badge(ctx, line->code, line->type, (180 - bw) / 2, 6, code_font, menu_cell_layer_is_highlighted(cell_layer));
		graphics_context_set_text_color(ctx, text_color);
		graphics_draw_text(ctx, line->dir, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(0, 30, 180, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

		GFont time_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
		if (show_mins) {
			// Clock time is the headline with the countdown in parentheses beside it
			// ("11:57 (3 min)"). The time keeps its realtime color (green when live);
			// the parenthesized countdown uses the normal text color. The two parts are
			// measured and centered together as a single line.
			char head[16];
			snprintf(head, sizeof(head), "%s ", line->time);
			char paren[16];
			snprintf(paren, sizeof(paren), "(%s)", mins_buf);
			GSize hs = graphics_text_layout_get_content_size(head, time_font, GRect(0, 0, 180, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft);
			GSize ps = graphics_text_layout_get_content_size(paren, time_font, GRect(0, 0, 180, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft);
			int16_t x = (180 - (hs.w + ps.w)) / 2;
			graphics_context_set_text_color(ctx, time_color);
			graphics_draw_text(ctx, head, time_font, GRect(x, 52, hs.w + 4, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
			graphics_context_set_text_color(ctx, text_color);
			graphics_draw_text(ctx, paren, time_font, GRect(x + hs.w, 52, ps.w + 4, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
		} else {
			// No countdown (departure is 10+ min out): just the clock time.
			graphics_context_set_text_color(ctx, time_color);
			graphics_draw_text(ctx, line->time, time_font, GRect(0, 52, 180, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
		}
	#elif PBL_PLATFORM_EMERY
		// One line: code badge, then destination, with the time stacked over the
		// "x min" countdown at the right edge. Everything is vertically centered.
		// 7px side padding keeps the content off the cell edges.
		{
		GRect bounds = layer_get_bounds(cell_layer);
		bool hl = menu_cell_layer_is_highlighted(cell_layer);
		GColor row_bg = hl ? GColorLightGray : GColorWhite;
		int16_t mid = bounds.size.h / 2;

		GFont code_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
		GSize code_sz = graphics_text_layout_get_content_size(line->code, code_font, GRect(0, 0, 90, 40), GTextOverflowModeWordWrap, GTextAlignmentLeft);
		int16_t badge_h = code_sz.h + 2;
		int16_t dir_x = 7 + (code_sz.w + 12) + 6;   // badge (12 = inner padding), 6px gap

		// Reserve only the time's actual width on the right (an "HH:MM" time is wider
		// than the "x min" countdown below it), leaving the destination the rest of
		// the row instead of a fixed, over-wide column.
		GFont time_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
		GSize time_sz = graphics_text_layout_get_content_size(line->time, time_font, GRect(0, 0, 90, 40), GTextOverflowModeWordWrap, GTextAlignmentLeft);
		int16_t time_right = bounds.size.w - 7;
		int16_t dir_right = time_right - time_sz.w - 6;   // 6px gap before the time

		// Destination first, so the badge and the time column paint over any
		// scrolled-off text. It scrolls when this row is focused and overflows.
		marquee_draw_label(ctx, cell_layer, line->dir, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), text_color, GRect(dir_x, mid - 12, dir_right - dir_x, 24), row_bg);

		// Code badge, vertically centered, over the destination's left gutter.
		draw_code_badge(ctx, line->code, line->type, 7, (bounds.size.h - badge_h) / 2, code_font, hl);

		// Mask any scrolled destination tail out of the time column (marquee only
		// masks its left side), then draw the time over the "x min" countdown, both
		// right-aligned at the edge.
		graphics_context_set_fill_color(ctx, row_bg);
		graphics_fill_rect(ctx, GRect(dir_right, 0, bounds.size.w - dir_right, bounds.size.h), 0, GCornerNone);
		int16_t tw = time_right - dir_right;
		graphics_context_set_text_color(ctx, time_color);
		if (show_mins) {
			graphics_draw_text(ctx, line->time, time_font, GRect(dir_right, mid - 20, tw, 21), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
			graphics_context_set_text_color(ctx, text_color);
			graphics_draw_text(ctx, mins_buf, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(dir_right, mid + 2, tw, 16), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
		} else {
			graphics_draw_text(ctx, line->time, time_font, GRect(dir_right, mid - 11, tw, 22), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
		}
		}
	#else
		draw_code_badge(ctx, line->code, line->type, 4, 6, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), menu_cell_layer_is_highlighted(cell_layer));
		// Scrolls the destination when this row is focused and it overflows. The
		// dir line sits below the badge with nothing to its left, so the cell's own
		// left edge clips the scrolled-off text (no gutter mask needed).
		marquee_draw_label(ctx, cell_layer, line->dir, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), text_color, GRect(4, 34, 136, 20), COLOR_FALLBACK(LINES_HL_COLOR, GColorBlack));
		graphics_context_set_text_color(ctx, time_color);
		graphics_draw_text(ctx, line->time, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(78, 0, 62, 21), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
		if (show_mins) {
			graphics_context_set_text_color(ctx, text_color);
			graphics_draw_text(ctx, mins_buf, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(78, 19, 62, 16), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
		}
	#endif
}

void lines_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
#ifdef SHOW_LATER_ENABLED
	// Only the "Show later" row is actionable; selecting a departure does nothing.
	if (!is_show_later_row(cell_index) || lineAmount == 0) {
		return;
	}

	// Empty the menu and show the loading indicator while the next window loads.
	// The departures stay in `lines` so they can be restored if there are no more.
	// The menu layer is hidden outright (not just reloaded to zero rows): reloading
	// a populated menu to empty can leave its section header drawn, which would show
	// a second stop-name title behind the loading overlay. Going through the empty
	// state also resets the menu to the top with the first row selected, so the
	// populated next window starts at its first departure.
	prev_line_amount = lineAmount;
	lineAmount = 0;
	if (lineMenuLayer) {
		layer_set_hidden(menu_layer_get_layer(lineMenuLayer), true);
		menu_layer_reload_data(lineMenuLayer);
	}
	if (loadingLayer) {
		text_layer_set_text(loadingLayer, "Loading..");
	}
	lines_set_loading(true);
	lines_request_departures(LINE_REQ_MORE);
#endif
}

// Long-pressing a departure pins/unpins the stop it belongs to, tagged with the
// stop's vehicle mode so the pinned list shows the right badge. The "Show later"
// row has no pin action.
void lines_long_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
#ifdef SHOW_LATER_ENABLED
	if (is_show_later_row(cell_index)) {
		return;
	}
#endif
	char type[2] = { stopType, '\0' };
	pins_show_action_menu(stopCode, stopName, type, "Pin stop", "Unpin stop");
}

void setup_lines_layer(Window *window, Layer *window_layer)
{
	GRect window_bounds = layer_get_bounds(window_layer);
	window_bounds.origin.y = STATUS_BAR_LAYER_HEIGHT;

	lineMenuLayer = menu_layer_create(window_bounds);
	menu_layer_set_callbacks(lineMenuLayer, NULL, (MenuLayerCallbacks){
		// The departures list shows a stop-name header on every platform, including
		// the round display (unlike the other menus, which drop it on round).
		#if defined(PBL_RECT) || defined(PBL_ROUND)
		 .get_num_sections = lines_get_num_sections_callback,
		 .get_header_height = lines_get_header_height_callback,
		 .draw_header = lines_draw_header_callback,
		#endif
		.get_num_rows = lines_get_num_rows_callback,
		.get_cell_height = lines_get_cell_height_callback,
		.draw_row = lines_draw_row_callback,
		.select_click = lines_select_callback,
		.select_long_click = lines_long_select_callback,
		.selection_changed = marquee_selection_changed,
	});

	menu_layer_set_highlight_colors(lineMenuLayer, COLOR_FALLBACK(LINES_HL_COLOR, GColorBlack), COLOR_FALLBACK(GColorBlack, GColorWhite));
	menu_layer_set_click_config_onto_window(lineMenuLayer, window);

	layer_add_child(window_layer, menu_layer_get_layer(lineMenuLayer));
}

// Title (the stop name) + centered "Loading.." text, shown over the (empty) menu
// until departures arrive. The MenuLayer does not draw its section header while
// it has no rows, so the title is shown here explicitly during loading.
void setup_lines_loading_layer(Layer *window_layer)
{
	GRect bounds = layer_get_bounds(window_layer);
	int16_t top = STATUS_BAR_LAYER_HEIGHT;

#ifndef PBL_ROUND
	// During loading the stop name is shown as a centered title, given the same
	// colored bar as the populated header so the background does not flash when the
	// departures land. Color watches only; B&W keeps a plain white title. The round
	// display skips this: it shows only the "Loading.." text and brings in its header
	// once the departures (and the stop's mode) are known.
	GColor title_bg = GColorClear;
	GColor title_fg = GColorBlack;
#ifdef PBL_COLOR
	if (stopType == 'B' || stopType == 'T') {
		title_bg = region_mode_color(stopCode, stopType);
		title_fg = GColorWhite;
	}
#endif
	// Match the populated header height (taller on emery) so the colored bar does
	// not change size when the departures land.
	titleLayer = text_layer_create(GRect(0, top, bounds.size.w, LINES_HEADER_HEIGHT));
	text_layer_set_background_color(titleLayer, title_bg);
	text_layer_set_text_color(titleLayer, title_fg);
	text_layer_set_font(titleLayer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
	text_layer_set_text_alignment(titleLayer, GTextAlignmentCenter);
	text_layer_set_text(titleLayer, stopName);
	layer_add_child(window_layer, text_layer_get_layer(titleLayer));
#endif

	int16_t cy = top + (bounds.size.h - top) / 2 - 12;
	loadingLayer = text_layer_create(GRect(0, cy, bounds.size.w, 24));
	text_layer_set_background_color(loadingLayer, GColorClear);
	text_layer_set_text_color(loadingLayer, GColorBlack);
	text_layer_set_font(loadingLayer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
	text_layer_set_text_alignment(loadingLayer, GTextAlignmentCenter);
	text_layer_set_text(loadingLayer, "Loading..");
	layer_add_child(window_layer, text_layer_get_layer(loadingLayer));
}

// Draws the fare-zone badge (a small filled circle with the zone letter) in the
// top-right corner of the status bar. On color watches the circle is blue, on
// black-and-white watches it is black; the letter is always white. Nothing is
// drawn when the stop has no zone.
static void zone_update_proc(Layer *layer, GContext *ctx)
{
	if (stopZone[0] == '\0') {
		return;
	}

	GRect bounds = layer_get_bounds(layer);
	const int16_t r = 5;       // badge radius
	const int16_t margin = 4;  // gap from the right edge to the circle
	int16_t cx = bounds.size.w - margin - r;
	// Nudge the center up a pixel so the circle clears the dotted separator drawn
	// at the bottom of the status bar.
	int16_t cy = bounds.size.h / 2 - 1;

	graphics_context_set_fill_color(ctx, COLOR_FALLBACK(GColorCobaltBlue, GColorBlack));
	graphics_fill_circle(ctx, GPoint(cx, cy), r);

	graphics_context_set_text_color(ctx, GColorWhite);
	// Pebble draws a glyph near the top of its box with descender space below, so
	// nudge the letter up to sit centered in the circle.
	graphics_draw_text(ctx, stopZone, fonts_get_system_font(FONT_KEY_GOTHIC_14),
		GRect(cx - r, cy - r - 4, 2 * r + 1, 2 * r + 5), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

// Builds the window's layers (menu + loading overlay + status bar). Split out so
// it can run both on initial load and when the window is revealed again after
// being covered (see lines_window_appear/disappear). No-op if already built.
static void lines_build_ui(Window *window)
{
	if (lineMenuLayer) {
		return;
	}
	Layer *window_layer = window_get_root_layer(window);

	setup_lines_layer(window, window_layer);
	setup_lines_loading_layer(window_layer);

#if defined(PBL_PLATFORM_EMERY) || defined(PBL_ROUND)
	busIcon = gdraw_command_image_create_with_resource(RESOURCE_ID_IMAGE_BUS);
	tramIcon = gdraw_command_image_create_with_resource(RESOURCE_ID_IMAGE_TRAM);
#endif

	statusLayer = status_bar_layer_create();
	status_bar_layer_set_separator_mode(statusLayer, StatusBarLayerSeparatorModeDotted);
	status_bar_layer_set_colors(statusLayer, GColorClear, GColorBlack);
	layer_add_child(window_layer, status_bar_layer_get_layer(statusLayer));

	// The zone badge sits on top of the status bar, spanning its width so the badge
	// can be right-aligned within it.
	GRect status_bounds = layer_get_bounds(window_layer);
	status_bounds.size.h = STATUS_BAR_LAYER_HEIGHT;
	zoneLayer = layer_create(status_bounds);
	layer_set_update_proc(zoneLayer, zone_update_proc);
	layer_add_child(window_layer, zoneLayer);
}

// Frees the window's layers. The departures themselves live in `lines`, so the
// menu is rebuilt from them on the next reveal. Freeing the layers while another
// window is on top keeps aplite's small heap available for that window.
static void lines_destroy_ui(void)
{
	if (lineMenuLayer) {
		savedSelectedRow = menu_layer_get_selected_index(lineMenuLayer).row;
		menu_layer_destroy(lineMenuLayer);
		lineMenuLayer = NULL;
	}
	if (zoneLayer) {
		layer_destroy(zoneLayer);
		zoneLayer = NULL;
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
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_ROUND)
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

// Requests a window of the selected stop's departing lines from the JS component.
// This replaces the underlying window's inbox handler while departures are on
// top. A REFRESH (the once-a-minute auto update) is "silent": the loading
// indicator and completion vibration are suppressed so the list updates in place.
// The stop code is sent under a key that tells JS which window to return.
static void lines_request_departures(LineRequestType type)
{
	line_silent_refresh = (type == LINE_REQ_REFRESH);

	app_message_register_inbox_received(lines_message_inbox);
	line_transfer_started = false;

	DictionaryIterator *iter;
	app_message_outbox_begin(&iter);
	if (iter == NULL) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "DictionaryIterator NULL!");
		return;
	}
	uint32_t key = type == LINE_REQ_MORE ? MESSAGE_KEY_lineMore
		: type == LINE_REQ_REFRESH ? MESSAGE_KEY_lineRefresh
		: MESSAGE_KEY_lineMessage;
	dict_write_cstring(iter, key, (char *)stopCode);
	dict_write_end(iter);
	app_message_outbox_send();
}

// Fired once a minute (on the wall-clock minute boundary) while the departures
// screen is showing. Silently re-fetches so the times stay current and any new
// departures appear without a loading flash.
static void lines_minute_tick(struct tm *tick_time, TimeUnits units_changed)
{
	// Skip while a foreground load is on screen (initial load, a "Show later"
	// fetch, or the "No later departures" notice): its own completion updates the
	// list, and a silent refresh would overwrite that work or the retained list.
	if (loadingLayer && !layer_get_hidden(text_layer_get_layer(loadingLayer))) {
		return;
	}
	lines_request_departures(LINE_REQ_REFRESH);
}

void lines_window_load(Window *window)
{
	// Clear any departures left over from a previous visit so the (reused) menu
	// does not render stale rows (and its header) behind the loading overlay.
	lineAmount = 0;
	// Drop any zone/code from a previously viewed stop; this stop's values arrive
	// with its departures, and an absent one should show no badge.
	stopZone[0] = '\0';
	stopShortCode[0] = '\0';
	lines_build_ui(window);
	lines_set_loading(true);

	lines_request_departures(LINE_REQ_LOAD);
}

// Rebuild the layers if they were freed while this window was covered (e.g. by the
// error, action-menu or feedback window), re-rendering the retained departures.
void lines_window_appear(Window *window)
{
	if (!lineMenuLayer) {
		lines_build_ui(window);
		menu_layer_reload_data(lineMenuLayer);
		lines_set_loading(lineAmount == 0);
		// Restore the row the user left on, clamped to the rows now on screen
		// (which include the "Show later" row where enabled).
		uint16_t rows = lines_get_num_rows_callback(lineMenuLayer, 0, NULL);
		if (rows > 0) {
			uint16_t row = savedSelectedRow < rows ? savedSelectedRow : (uint16_t)(rows - 1);
			menu_layer_set_selected_index(lineMenuLayer, MenuIndex(0, row), MenuRowAlignCenter, false);
		}
	}

	app_message_register_inbox_received(lines_message_inbox);

	// Keep the departure times current while the user waits for the bus: refresh
	// once a minute, but only while this screen is actually showing. The matching
	// unsubscribe in the disappear handler stops covered/other screens (menu,
	// pins, stop) from being periodically updated.
	tick_timer_service_subscribe(MINUTE_UNIT, lines_minute_tick);

	// Drive the scrolling destination names while this list is on screen.
	marquee_attach(lineMenuLayer);
}

// Free the layers whenever another window covers this one, returning their memory
// to the heap for the window on top.
void lines_window_disappear(Window *window)
{
	marquee_detach(lineMenuLayer);
	tick_timer_service_unsubscribe();
#ifdef SHOW_LATER_ENABLED
	// Cancel a pending "No later departures" restore so it cannot fire against the
	// freed layers, and put back the retained count it would have restored so the
	// departures in `lines` re-render (rather than staying blank) on the next reveal.
	if (no_more_timer) {
		app_timer_cancel(no_more_timer);
		no_more_timer = NULL;
		lineAmount = prev_line_amount;
		if (loadingLayer) {
			text_layer_set_text(loadingLayer, "Loading..");
		}
	}
#endif
	lines_destroy_ui();
}

void lines_window_unload(Window *window)
{
	marquee_detach(lineMenuLayer);
	lines_destroy_ui();

	// The window underneath (stops or pinned stops) reclaims the AppMessage inbox in
	// its own appear handler, so nothing needs to be restored here.
}

void lines_window_create()
{
	linesWindow = window_create();
	window_set_window_handlers(linesWindow, (WindowHandlers) {
		.load = lines_window_load,
		.appear = lines_window_appear,
		.disappear = lines_window_disappear,
		.unload = lines_window_unload
	});
}

void lines_window_destroy()
{
	window_destroy(linesWindow);
}

Window *lines_window_get_window()
{
	return linesWindow;
}
