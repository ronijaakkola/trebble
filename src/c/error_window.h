#pragma once

// Selects which icon the error screen shows above the message.
typedef enum {
	ERROR_ICON_GENERIC = 0, // generic error bitmap (IMAGE_ERROR)
	ERROR_ICON_NO_PHONE,    // no phone connection (PDC vector icon)
	ERROR_ICON_NO_INTERNET, // no internet connection (PDC vector icon)
} ErrorIcon;

// Set the error message and icon shown the next time the window is pushed.
// NOTE: This has to be called before pushing the window to the stack.
void error_window_set_error(const char *errorMessage, ErrorIcon icon);
void error_window_show();
void error_window_create();
void error_window_destroy();
Window *error_window_get_window();
