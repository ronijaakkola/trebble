#pragma once

#ifdef LOW_MEMORY_DEVICE
#define MAX_INBOX_SIZE 512
#else
#define MAX_INBOX_SIZE 4096
#endif
#define MAX_OUTBOX_SIZE 64

#define LOADING_BG_COLOR GColorTiffanyBlue
#define NUM_STOPS 10

struct StopInfo {
	char code[20];
	char name[30];
	int dist;
};

void s_loading_window_create();
void s_loading_window_destroy();
Window *s_loading_window_get_window();
