#pragma once
#include <pebble.h>

void feedback_window_create(void);
void feedback_window_destroy(void);

// Shows a brief, full-screen confirmation with the given message. The window
// slides in from the right, auto-dismisses after a short delay, and can also be
// dismissed immediately with any button.
void feedback_window_show(const char *message);
