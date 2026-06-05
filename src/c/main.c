#include <pebble.h>
#include "splash_window.h"
#include "home_window.h"
#include "main_window.h"
#include "lines_window.h"
#include "error_window.h"

#ifdef LOW_MEMORY_DEVICE
#define MAX_INBOX_SIZE 512
#else
#define MAX_INBOX_SIZE 4096
#endif
#define MAX_OUTBOX_SIZE 64

static void app_message_dropped(AppMessageResult reason, void *context) {
	APP_LOG(APP_LOG_LEVEL_INFO, "Message dropped, reason: %d", reason);
}

void open_home_screen(void *data) {
	// Remove splash window from the stack because we don't want to come back to it
	window_stack_remove(splash_window_get_window(), true);
	// Free the splash window to save memory
	splash_window_destroy();
	window_stack_push(home_window_get_window(), true);
}

void init()
{
	// Create all windows
	splash_window_create();
	home_window_create();
	main_window_create();
	lines_window_create();
	error_window_create();

	// Open the AppMessage channel once. The display windows register their own
	// inbox-received handlers when they load.
	app_message_register_inbox_dropped(app_message_dropped);
	app_message_open(MAX_INBOX_SIZE, MAX_OUTBOX_SIZE);

	// Push first screen to the stack
	window_stack_push(splash_window_get_window(), true);

	// Timeout to show splash screen only for a limited time
	app_timer_register(1500, open_home_screen, NULL);
}

void deinit()
{
	home_window_destroy();
	main_window_destroy();
	lines_window_destroy();
	error_window_destroy();
}

int main()
{
	init();
	app_event_loop();
	deinit();
}
