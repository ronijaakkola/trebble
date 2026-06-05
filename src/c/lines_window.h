#pragma once

#define LINES_HL_COLOR GColorLightGray
#define NUM_LINES 10

struct LineInfo {
	char time[10];
	char code[10];
	char dir[30];
	char type[2];
	bool realtime; // true when the departure time is a live prediction
	int mins;      // minutes until departure; <0 means "do not show"
};

// Stores the selected stop's code/name, then opens the departures window. The
// window fetches its own departures once shown.
void lines_window_show(char *code, char *name);
void lines_window_create();
void lines_window_destroy();
Window *lines_window_get_window();