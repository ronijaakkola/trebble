#pragma once
#include <pebble.h>

// Horizontal "marquee" scrolling for the focused row's label in a MenuLayer.
//
// Pebble has no built-in horizontal scrolling text, so this drives a single
// app_timer that shifts the focused, overflowing label left to reveal the hidden
// tail, pauses, snaps back to the start, and loops. Only one row is focused at a
// time across the whole app, so a single shared state/timer serves every list.
//
// Usage in a window:
//   - .appear:            marquee_attach(menuLayer);
//   - .disappear/.unload: marquee_detach(menuLayer);
//   - callbacks:          .selection_changed = marquee_selection_changed
//   - draw_row:           use marquee_draw_label() for the long label, and draw
//                         any left-side icon/dot AFTER it so the text slides under.

// Bind the marquee to the menu that is now on screen and (re)start its timer.
void marquee_attach(MenuLayer *menu);

// Unbind and stop the animation. Safe to call with a stale/null menu; only
// detaches if `menu` matches the attached one (or is NULL).
void marquee_detach(MenuLayer *menu);

// MenuLayer .selection_changed callback: restart the scroll cycle from the start
// whenever the focused row changes.
void marquee_selection_changed(MenuLayer *menu, MenuIndex new_index, MenuIndex old_index, void *context);

// Draws a single-line label left-aligned inside `frame`. When the cell is the
// focused (highlighted) row and the text is wider than `frame`, it scrolls;
// otherwise it is drawn static with a trailing ellipsis. `bg` is the row's
// background, used to mask the gutter to the left of `frame` (there is no
// clip-box API) so the scrolled text disappears at the frame's left edge. The
// caller should draw any icon/dot in that gutter AFTER this call.
void marquee_draw_label(GContext *ctx, const Layer *cell_layer, const char *text,
                        GFont font, GColor text_color, GRect frame, GColor bg);
