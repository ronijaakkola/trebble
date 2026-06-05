#pragma once
#include <pebble.h>

#define FAV_HL_COLOR GColorLightGray

void favorites_window_create(void);
void favorites_window_destroy(void);
Window *favorites_window_get_window(void);

// Pushes the favorites window onto the stack. It fetches live distances for the
// favorited stops and lists them nearest-first.
void favorites_window_show(void);

// Re-registers the favorites AppMessage inbox handler. Used when returning from
// the departures window so reselecting a favorite keeps working.
void favorites_window_register_inbox(void);
