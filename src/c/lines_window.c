#include <pebble.h>
#include "lines_window.h"
#include "main_window.h"
#include "error_window.h"
#include "favorites.h"

static Window *linesWindow;
static MenuLayer *lineMenuLayer;
static StatusBarLayer *statusLayer;
static TextLayer *loadingLayer;
static TextLayer *titleLayer;

static char stopCode[20];
char stopName[30];
static int lineAmount = 0;
static struct LineInfo lines[NUM_LINES];

static bool line_transfer_started;
static bool line_got_name;
static bool line_got_time;
static bool line_got_dir;
static int line_index;

void lines_window_show(char *code, char *name)
{
	strncpy(stopCode, code, sizeof(stopCode));
	strncpy(stopName, name, sizeof(stopName));
	window_stack_push(lines_window_get_window(), true);
}

// Shows the title + centered "Loading.." text while waiting for data, or hides
// them to reveal the populated departures menu (whose own header takes over).
static void lines_set_loading(bool loading)
{
	layer_set_hidden(text_layer_get_layer(loadingLayer), !loading);
	layer_set_hidden(text_layer_get_layer(titleLayer), !loading);
}

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
		error_window_set_error("No internet connection", ERROR_ICON_NO_INTERNET);
		error_window_show();
		return;
	}

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
			menu_layer_reload_data(lineMenuLayer);
			lines_set_loading(false);
			vibes_short_pulse();
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
			text_layer_set_text(loadingLayer, "No departures");
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
			return lineAmount;
		default:
			return 0;
	}
}

int16_t lines_get_cell_height_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	if(cell_index->section == 0) {
		#if defined(PBL_ROUND)
			return 86; // Extra room for the "X min" line under the time
		#elif defined(PBL_PLATFORM_EMERY)
			return 64;
		#else
			return 60; // Tall enough to pad the badge and destination top/bottom
		#endif
	}
	else return 44; //Default height of cell
}

int16_t lines_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
	return MENU_CELL_BASIC_HEADER_HEIGHT;
}

void lines_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data)
{
	if (section_index != 0) {
		return;
	}
	// Centered header, matching the centered title shown during loading so the
	// title does not shift when the list appears.
	GRect bounds = layer_get_bounds(cell_layer);
	graphics_context_set_text_color(ctx, GColorBlack);
	graphics_draw_text(ctx, (char *)stopName, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// Picks the badge background color for a line type. On color watches buses are
// blue and trams red; on black-and-white watches both fall back to black, where
// the type is not distinguished.
static GColor badge_color_for_type(const char *type)
{
	if (type[0] == 'B') {
		return COLOR_FALLBACK(GColorCobaltBlue, GColorBlack);
	}
	if (type[0] == 'T') {
		return COLOR_FALLBACK(GColorRed, GColorBlack);
	}
	// Unknown mode: neutral badge so the line code stays readable.
	return GColorBlack;
}

#ifdef PBL_ROUND
static int16_t code_badge_width(GContext *ctx, const char *code, GFont font)
{
	GSize text = graphics_text_layout_get_content_size(code, font, GRect(0, 0, 90, 40), GTextOverflowModeWordWrap, GTextAlignmentLeft);
	return text.w + 10;
}
#endif

// Draws the line code inside a rounded, type-colored badge at (x, y).
static void draw_code_badge(GContext *ctx, const char *code, const char *type, int16_t x, int16_t y, GFont font)
{
	GSize text = graphics_text_layout_get_content_size(code, font, GRect(0, 0, 90, 40), GTextOverflowModeWordWrap, GTextAlignmentLeft);
	int16_t bw = text.w + 12;
	int16_t bh = text.h + 2;

	graphics_context_set_fill_color(ctx, badge_color_for_type(type));
	graphics_fill_rect(ctx, GRect(x, y, bw, bh), 4, GCornersAll);

	graphics_context_set_text_color(ctx, GColorWhite);
	// Pebble draws the glyph near the top of its box with descender space below,
	// so a number (no descenders) looks off-center. Nudge it to center it vertically.
	graphics_draw_text(ctx, code, font, GRect(x + 6, y - 3, bw, bh + 4), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

void lines_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data)
{
	if (cell_index->section != 0) {
		return;
	}

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
		draw_code_badge(ctx, line->code, line->type, (180 - bw) / 2, 6, code_font);
		graphics_context_set_text_color(ctx, text_color);
		graphics_draw_text(ctx, line->dir, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(0, 30, 180, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
		graphics_context_set_text_color(ctx, time_color);
		graphics_draw_text(ctx, line->time, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(0, 50, 180, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
		if (show_mins) {
			graphics_context_set_text_color(ctx, text_color);
			graphics_draw_text(ctx, mins_buf, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(0, 70, 180, 16), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
		}
	#elif PBL_PLATFORM_EMERY
		draw_code_badge(ctx, line->code, line->type, 4, 6, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
		graphics_context_set_text_color(ctx, text_color);
		graphics_draw_text(ctx, line->dir, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(4, 34, 150, 24), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
		graphics_context_set_text_color(ctx, time_color);
		graphics_draw_text(ctx, line->time, fonts_get_system_font(FONT_KEY_GOTHIC_24), GRect(135, 0, 62, 24), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
		if (show_mins) {
			graphics_context_set_text_color(ctx, text_color);
			graphics_draw_text(ctx, mins_buf, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(135, 28, 62, 20), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
		}
	#else
		draw_code_badge(ctx, line->code, line->type, 4, 6, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
		graphics_context_set_text_color(ctx, text_color);
		graphics_draw_text(ctx, line->dir, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(4, 34, 136, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
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
	return;
}

// Long-pressing a departure favorites/unfavorites the stop it belongs to. The
// stop's vehicle mode is not known here, so the type is left blank; the favorites
// list fills it in from live data.
void lines_long_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
	favorites_show_action_menu(stopCode, stopName, "", "Favorite stop", "Unfavorite stop");
}

void setup_lines_layer(Window *window, Layer *window_layer)
{
	GRect window_bounds = layer_get_bounds(window_layer);
	window_bounds.origin.y = STATUS_BAR_LAYER_HEIGHT;

	lineMenuLayer = menu_layer_create(window_bounds);
	menu_layer_set_callbacks(lineMenuLayer, NULL, (MenuLayerCallbacks){
		#ifdef PBL_RECT
		 .get_num_sections = lines_get_num_sections_callback,
		 .get_header_height = lines_get_header_height_callback,
		 .draw_header = lines_draw_header_callback,
		#endif
		.get_num_rows = lines_get_num_rows_callback,
		.get_cell_height = lines_get_cell_height_callback,
		.draw_row = lines_draw_row_callback,
		.select_click = lines_select_callback,
		.select_long_click = lines_long_select_callback,
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

	GTextAlignment title_align = GTextAlignmentCenter;
	titleLayer = text_layer_create(GRect(4, top, bounds.size.w - 8, 18));
	text_layer_set_background_color(titleLayer, GColorClear);
	text_layer_set_text_color(titleLayer, GColorBlack);
	text_layer_set_font(titleLayer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
	text_layer_set_text_alignment(titleLayer, title_align);
	text_layer_set_text(titleLayer, stopName);
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

void lines_window_load(Window *window)
{
	Layer *window_layer = window_get_root_layer(window);
	// Clear any departures left over from a previous visit so the (reused) menu
	// does not render stale rows (and its header) behind the loading overlay.
	lineAmount = 0;
	setup_lines_layer(window, window_layer);
	setup_lines_loading_layer(window_layer);

	statusLayer = status_bar_layer_create();
  status_bar_layer_set_separator_mode(statusLayer, StatusBarLayerSeparatorModeDotted);
  status_bar_layer_set_colors(statusLayer, GColorClear, GColorBlack);
	layer_add_child(window_layer, status_bar_layer_get_layer(statusLayer));

	lines_set_loading(true);

	// Request departing lines for the selected stop from the JS component. This
	// replaces the stops window's inbox handler while departures are on top.
	app_message_register_inbox_received(lines_message_inbox);
	line_transfer_started = false;

	DictionaryIterator *iter;
	app_message_outbox_begin(&iter);
	if (iter == NULL) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "DictionaryIterator NULL!");
		return;
	}
	dict_write_cstring(iter, MESSAGE_KEY_lineMessage, (char *)stopCode);
	dict_write_end(iter);
	app_message_outbox_send();
}

void lines_window_unload(Window *window)
{
	menu_layer_destroy(lineMenuLayer);
	status_bar_layer_destroy(statusLayer);
	text_layer_destroy(loadingLayer);
	text_layer_destroy(titleLayer);

	// The window underneath (stops or favorites) reclaims the AppMessage inbox in
	// its own appear handler, so nothing needs to be restored here.
}

void lines_window_create()
{
	linesWindow = window_create();
	window_set_window_handlers(linesWindow, (WindowHandlers) {
		.load = lines_window_load,
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
