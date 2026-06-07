#pragma once
#include <pebble.h>

#define PIN_HL_COLOR GColorLightGray

void pins_window_create(void);
void pins_window_destroy(void);
Window *pins_window_get_window(void);

// Pushes the pinned stops window onto the stack. It fetches live distances for
// the pinned stops and lists them nearest-first.
void pins_window_show(void);

// Re-registers the pinned stops AppMessage inbox handler. Used when returning
// from the departures window so reselecting a pinned stop keeps working.
void pins_window_register_inbox(void);
