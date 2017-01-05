#include <pebble.h>
#include "splash_window.h"
#include "s_loading_window.h"
#include "main_window.h"
#include "l_loading_window.h"
#include "lines_window.h"
#include "error_window.h"

void open_main_menu(void *data) {
	// Remove splash window from the stack because we don't want to come back to it
	window_stack_remove(splash_window_get_window(), true);
	// Free the splash window to save memory
	splash_window_destroy();
	window_stack_push(s_loading_window_get_window(), true);
}

void init() 
{
	splash_window_create();
	s_loading_window_create();
	main_window_create();
	l_loading_window_create();
	lines_window_create();
	error_window_create();
	window_stack_push(splash_window_get_window(), true);
	
	app_timer_register(1500, open_main_menu, NULL);
}

void deinit() 
{
	s_loading_window_destroy();
	main_window_destroy();
	l_loading_window_destroy();
	lines_window_destroy();
	error_window_destroy();
}

int main() 
{
	init();
	app_event_loop();
	deinit();
}
