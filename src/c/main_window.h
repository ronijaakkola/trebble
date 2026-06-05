#pragma once

#define MENU_HL_COLOR GColorLightGray
#define NUM_STOPS 10

struct StopInfo {
	char code[20];
	char name[30];
	int dist;
	char type[2];
};

void main_window_create();
void main_window_destroy();
Window *main_window_get_window();
// Re-registers the stops AppMessage inbox handler. Called by lines_window when it
// closes so the stops window keeps working if another stop is selected.
void main_window_register_inbox();
