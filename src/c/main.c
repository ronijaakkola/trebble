#include <pebble.h>
#include "splash_window.h"
#include "home_window.h"
#include "main_window.h"
#include "lines_window.h"
#include "error_window.h"
#include "pins.h"
#include "pins_window.h"
#include "feedback_window.h"

// Aplite has only ~24KB of app heap, so a 4KB inbox would starve the departures
// window (its menu then runs out of memory mid-scroll and the app faults). A
// 512-byte inbox comfortably holds a single stop/line message there; the roomier
// platforms keep the larger buffer.
#ifdef PBL_PLATFORM_APLITE
#define MAX_INBOX_SIZE 512
#else
#define MAX_INBOX_SIZE 4096
#endif
// Large enough to hold the comma-separated list of pinned stop codes sent when
// opening the Pinned stops window.
#define MAX_OUTBOX_SIZE 256

static void app_message_dropped(AppMessageResult reason, void *context) {
	APP_LOG(APP_LOG_LEVEL_INFO, "Message dropped, reason: %d", reason);
}

// Tracks whether the no-phone error is the one we put on the stack, so it is
// only auto-dismissed when the phone reconnects.
static bool no_phone_error_showing = false;

// Shows the no-phone error while the phone (Pebble app) is unreachable and
// clears it again once the connection is restored. Nothing the watch can do
// reaches the network without the phone, so this takes precedence.
static void update_phone_connection(bool connected) {
	if (!connected && !no_phone_error_showing) {
		no_phone_error_showing = true;
		error_window_set_error("No phone connection", ERROR_ICON_NO_PHONE);
		error_window_show();
	} else if (connected && no_phone_error_showing) {
		no_phone_error_showing = false;
		window_stack_remove(error_window_get_window(), true);
	}
}

static void app_connection_handler(bool connected) {
	update_phone_connection(connected);
}

void open_home_screen(void *data) {
	// Remove splash window from the stack because we don't want to come back to it
	window_stack_remove(splash_window_get_window(), true);
	// Free the splash window to save memory
	splash_window_destroy();
	window_stack_push(home_window_get_window(), true);

	// From here on, watch the phone connection: surface the no-phone error now if
	// we are already disconnected, and whenever it drops later.
	connection_service_subscribe((ConnectionHandlers) {
		.pebble_app_connection_handler = app_connection_handler
	});
	update_phone_connection(connection_service_peek_pebble_app_connection());
}

void init()
{
	// Load persisted pins before any window that reads them is shown.
	pins_load();

#ifdef SCREENSHOT_MODE
	// Screenshot builds replace the pin list with a fixed fixture so the home-menu
	// count and the pinned list are identical every run (see fixtures.js / the
	// release-build skill). Compiled out of normal builds.
	pins_seed_fixtures();
#endif

	// Create all windows
	splash_window_create();
	home_window_create();
	main_window_create();
	lines_window_create();
	error_window_create();
	pins_window_create();
	feedback_window_create();

	// Open the AppMessage channel once. The display windows register their own
	// inbox-received handlers when they load.
	app_message_register_inbox_dropped(app_message_dropped);
	app_message_open(MAX_INBOX_SIZE, MAX_OUTBOX_SIZE);

	// Push first screen to the stack
	window_stack_push(splash_window_get_window(), true);

	// Start resolving the menu-header city now, while the splash is up, so its reply
	// has time to land before the home menu appears (no visible "Loading.." → city
	// swap). This never blocks the splash: the menu opens on the timer below
	// regardless, and the header degrades on its own if the lookup is slow or fails.
	home_window_start_location_lookup();

	// Show the splash for a short minimum (long enough to read the wordmark) before
	// revealing the home menu. The city lookup above runs concurrently.
	app_timer_register(2000, open_home_screen, NULL);
}

void deinit()
{
	home_window_destroy();
	main_window_destroy();
	lines_window_destroy();
	error_window_destroy();
	pins_window_destroy();
	feedback_window_destroy();
}

int main()
{
	init();
	app_event_loop();
	deinit();
}
