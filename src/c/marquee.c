#include "marquee.h"

// Animation tuning. The timer ticks at TICK_MS; each scroll tick advances the
// text by STEP_PX. The label holds still for PAUSE_TICKS at both the start
// (before scrolling) and the end (before snapping back).
#define TICK_MS      50
#define STEP_PX      2
#define PAUSE_TICKS  29  // ~1450 ms at TICK_MS

enum { MQ_PHASE_START, MQ_PHASE_SCROLL, MQ_PHASE_END };

static MenuLayer *s_menu;
static AppTimer  *s_timer;
static int16_t    s_offset;       // current scroll offset in px (0..s_max_offset)
static int16_t    s_max_offset;   // px the focused label overflows its frame
static int        s_phase;
static int        s_phase_ticks;
static bool       s_measured;     // has the focused label reported its overflow yet?

static void timer_cb(void *data);

static void ensure_timer(void)
{
	if (!s_timer && s_menu) {
		s_timer = app_timer_register(TICK_MS, timer_cb, NULL);
	}
}

// Reset the cycle to "paused at the start", overflow not yet known. The next
// render of the focused row reports the overflow via marquee_draw_label().
static void reset_state(void)
{
	s_offset = 0;
	s_max_offset = 0;
	s_phase = MQ_PHASE_START;
	s_phase_ticks = 0;
	s_measured = false;
}

static void timer_cb(void *data)
{
	s_timer = NULL;
	if (!s_menu) {
		return;
	}

	// Wait until the focused row has drawn once and reported its overflow. The
	// menu repaints on attach/selection-change before this first tick fires, so
	// a long label is already measured by now; if the focused row has no marquee
	// label (e.g. the "Show later" action), it never reports, so we simply idle
	// here rather than busy-repainting.
	if (!s_measured) {
		return;
	}
	if (s_max_offset <= 0) {
		// Focused label fits — nothing to animate until the next selection change.
		return;
	}

	switch (s_phase) {
		case MQ_PHASE_START:
			if (++s_phase_ticks >= PAUSE_TICKS) {
				s_phase = MQ_PHASE_SCROLL;
				s_phase_ticks = 0;
			}
			break;
		case MQ_PHASE_SCROLL:
			s_offset += STEP_PX;
			if (s_offset >= s_max_offset) {
				s_offset = s_max_offset;
				s_phase = MQ_PHASE_END;
				s_phase_ticks = 0;
			}
			break;
		case MQ_PHASE_END:
			if (++s_phase_ticks >= PAUSE_TICKS) {
				s_offset = 0;
				s_phase = MQ_PHASE_START;
				s_phase_ticks = 0;
			}
			break;
	}

	layer_mark_dirty(menu_layer_get_layer(s_menu));
	ensure_timer();
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
