#pragma once

#define LOADING_BG_COLOR GColorTiffanyBlue
#define NUM_LINES 10

struct LineInfo {
	char time[10];
	char code[10];
	char dir[30];
};

void l_loading_window_show();
void l_loading_window_create();
void l_loading_window_destroy();
Window *l_loading_window_get_window();
