#include "marquee.h"

// Animation tuning. The timer ticks at TICK_MS; each scroll tick advances the
// text by STEP_PX. The label holds still for PAUSE_TICKS at both the start
// (before scrolling) and the end (before snapping back).
#define TICK_MS      50
#define STEP_PX      2
#define PAUSE_TICKS  29  // ~1450 ms at TICK_MS

// START/END are the pauses at each extreme; SCROLL advances toward the tail.
// BACK is only used by ping-pong channels, scrolling smoothly back to the start
// instead of snapping there.
enum { MQ_PHASE_START, MQ_PHASE_SCROLL, MQ_PHASE_END, MQ_PHASE_BACK };

static MenuLayer *s_menu;
static AppTimer  *s_timer;
static int16_t    s_offset;       // current scroll offset in px (0..s_max_offset)
static int16_t    s_max_offset;   // px the focused label overflows its frame
static int        s_phase;
static int        s_phase_ticks;
static bool       s_measured;     // has the focused label reported its overflow yet?

// Second, always-on channel for a label that cannot be focused (a window header).
// It shares the timer with the focused-row channel above but animates on its own.
static int16_t    s_hdr_offset;
static int16_t    s_hdr_max_offset;
static int        s_hdr_phase;
static int        s_hdr_phase_ticks;
static bool       s_hdr_measured;

static void timer_cb(void *data);

static void ensure_timer(void)
{
	if (!s_timer && s_menu) {
		s_timer = app_timer_register(TICK_MS, timer_cb, NULL);
	}
}

// Reset the cycle to "paused at the start", overflow not yet known. The next
// render of the focused row reports the overflow via marquee_draw_label() (and
// the header via marquee_draw_auto_label()).
static void reset_state(void)
{
	s_offset = 0;
	s_max_offset = 0;
	s_phase = MQ_PHASE_START;
	s_phase_ticks = 0;
	s_measured = false;

	s_hdr_offset = 0;
	s_hdr_max_offset = 0;
	s_hdr_phase = MQ_PHASE_START;
	s_hdr_phase_ticks = 0;
	s_hdr_measured = false;
}

// Advances one scroll channel by a tick. When `pingpong` is false the cycle is
// start pause -> scroll to tail -> end pause -> snap back -> loop. When true the
// end pause is followed by a smooth scroll back to the start (start pause ->
// scroll -> end pause -> scroll back -> loop). Returns true if the channel has
// something to animate, so the timer should keep running.
static bool advance_channel(int16_t *offset, int16_t max_offset, int *phase, int *phase_ticks, bool pingpong)
{
	if (max_offset <= 0) {
		return false;
	}
	switch (*phase) {
		case MQ_PHASE_START:
			if (++(*phase_ticks) >= PAUSE_TICKS) {
				*phase = MQ_PHASE_SCROLL;
				*phase_ticks = 0;
			}
			break;
		case MQ_PHASE_SCROLL:
			*offset += STEP_PX;
			if (*offset >= max_offset) {
				*offset = max_offset;
				*phase = MQ_PHASE_END;
				*phase_ticks = 0;
			}
			break;
		case MQ_PHASE_END:
			if (++(*phase_ticks) >= PAUSE_TICKS) {
				*phase_ticks = 0;
				if (pingpong) {
					// Scroll smoothly back to the start rather than snapping.
					*phase = MQ_PHASE_BACK;
				} else {
					*offset = 0;
					*phase = MQ_PHASE_START;
				}
			}
			break;
		case MQ_PHASE_BACK:
			*offset -= STEP_PX;
			if (*offset <= 0) {
				*offset = 0;
				*phase = MQ_PHASE_START;
				*phase_ticks = 0;
			}
			break;
	}
	return true;
}

static void timer_cb(void *data)
{
	s_timer = NULL;
	if (!s_menu) {
		return;
	}

	// Advance whichever channels have reported an overflowing label. A channel
	// only reports once it has drawn (the focused row via marquee_draw_label, the
	// header via marquee_draw_auto_label); until then it idles instead of
	// busy-repainting. The timer keeps running while either channel is animating.
	bool active = false;
	if (s_measured) {
		active |= advance_channel(&s_offset, s_max_offset, &s_phase, &s_phase_ticks, false);
	}
	if (s_hdr_measured) {
		// The header scrolls back and forth, since it has no resting "focused" state
		// to snap back to.
		active |= advance_channel(&s_hdr_offset, s_hdr_max_offset, &s_hdr_phase, &s_hdr_phase_ticks, true);
	}

	if (active) {
		layer_mark_dirty(menu_layer_get_layer(s_menu));
		ensure_timer();
	}
}

// Records the focused label's overflow and returns the offset to draw it at.
static int16_t offset_for(int16_t overflow_px)
{
	s_measured = true;
	s_max_offset = overflow_px > 0 ? overflow_px : 0;
	if (s_offset > s_max_offset) {
		s_offset = s_max_offset;
	}
	// If the label overflows but the timer had gone idle, kick it back to life.
	if (s_max_offset > 0) {
		ensure_timer();
	}
	return s_offset;
}

// Header-channel counterpart of offset_for(): records the header label's overflow
// and returns the offset to draw it at.
static int16_t offset_for_header(int16_t overflow_px)
{
	s_hdr_measured = true;
	s_hdr_max_offset = overflow_px > 0 ? overflow_px : 0;
	if (s_hdr_offset > s_hdr_max_offset) {
		s_hdr_offset = s_hdr_max_offset;
	}
	if (s_hdr_max_offset > 0) {
		ensure_timer();
	}
	return s_hdr_offset;
}

void marquee_attach(MenuLayer *menu)
{
	s_menu = menu;
	reset_state();
	ensure_timer();
}

void marquee_detach(MenuLayer *menu)
{
	if (menu && menu != s_menu) {
		return;
	}
	if (s_timer) {
		app_timer_cancel(s_timer);
		s_timer = NULL;
	}
	s_menu = NULL;
	reset_state();
}

void marquee_selection_changed(MenuLayer *menu, MenuIndex new_index, MenuIndex old_index, void *context)
{
	reset_state();
	ensure_timer();
}

void marquee_draw_label(GContext *ctx, const Layer *cell_layer, const char *text,
                        GFont font, GColor text_color, GRect frame, GColor bg)
{
	graphics_context_set_text_color(ctx, text_color);

	if (menu_cell_layer_is_highlighted(cell_layer)) {
		// Measure the natural single-line width with a generously wide box so the
		// text never wraps, then compare against the available frame width.
		GSize full = graphics_text_layout_get_content_size(
			text, font, GRect(0, 0, 2000, frame.size.h + 4),
			GTextOverflowModeWordWrap, GTextAlignmentLeft);
		int16_t overflow = full.w - frame.size.w;
		int16_t ox = offset_for(overflow);

		if (overflow > 0) {
			// Draw the full text shifted left by the current offset. The box is
			// wide enough that it stays on one line; the part scrolled past the
			// frame's left edge is masked below (no clip-box API exists).
			graphics_draw_text(ctx, text, font,
				GRect(frame.origin.x - ox, frame.origin.y, full.w + 8, frame.size.h),
				GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
			if (frame.origin.x > 0) {
				graphics_context_set_fill_color(ctx, bg);
				graphics_fill_rect(ctx,
					GRect(0, frame.origin.y, frame.origin.x, frame.size.h),
					0, GCornerNone);
			}
			return;
		}
	}

	graphics_draw_text(ctx, text, font, frame,
		GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

void marquee_draw_auto_label(GContext *ctx, const char *text, GFont font,
                             GColor text_color, GRect frame, GColor bg,
                             int16_t mask_left, int16_t mask_right)
{
	graphics_context_set_text_color(ctx, text_color);

	// Measure the natural single-line width with a generously wide box so the text
	// never wraps, then compare against the available frame width.
	GSize full = graphics_text_layout_get_content_size(
		text, font, GRect(0, 0, 2000, frame.size.h + 4),
		GTextOverflowModeWordWrap, GTextAlignmentLeft);
	int16_t overflow = full.w - frame.size.w;
	int16_t ox = offset_for_header(overflow);

	if (overflow > 0) {
		// Draw the full text shifted left by the current offset, then mask both
		// gutters (no clip-box API exists) so only the part inside `frame` shows.
		graphics_draw_text(ctx, text, font,
			GRect(frame.origin.x - ox, frame.origin.y, full.w + 8, frame.size.h),
			GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

		graphics_context_set_fill_color(ctx, bg);
		if (frame.origin.x > mask_left) {
			graphics_fill_rect(ctx,
				GRect(mask_left, frame.origin.y, frame.origin.x - mask_left, frame.size.h),
				0, GCornerNone);
		}
		int16_t right_edge = frame.origin.x + frame.size.w;
		if (mask_right > right_edge) {
			graphics_fill_rect(ctx,
				GRect(right_edge, frame.origin.y, mask_right - right_edge, frame.size.h),
				0, GCornerNone);
		}
		return;
	}

	graphics_draw_text(ctx, text, font, frame,
		GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}
