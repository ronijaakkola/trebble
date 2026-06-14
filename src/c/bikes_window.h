#pragma once
#include <pebble.h>

// aplite has only ~24KB of app RAM; a full 10-station array of name strings leaves
// almost no heap for the window layers, so it holds fewer there. The roomier
// platforms keep the full list. The JS side always sends up to 10 (nearest first);
// any beyond the cap are simply dropped by the inbox handler.
// The City Bikes feature is not built on aplite: the original Pebble/Steel has only
// ~24KB of app RAM and the existing app is already at that ceiling, so adding a whole
// new window plus this array reliably OOMs on launch. The window body in
// bikes_window.c is compiled out there (see the PBL_PLATFORM_APLITE guard), and the
// home menu omits the row. Every other platform builds the full feature.
#define NUM_BIKE_STATIONS 10

// A nearby city-bike (bike-share) station. `bikes` is the number of bikes
// currently available; -1 means the count is unknown (the station is not reporting
// real-time data), shown as "n/a". `dist` is in meters (within the search radius, so
// int16 is ample). The stationId is intentionally not stored in v1: the list has no
// detail screen and no pinning, and on aplite the per-station array must stay small.
// (Reintroduce a code field when pinning bikes — Phase 4.)
struct BikeStationInfo {
	char name[30];
	int16_t dist;
	int16_t bikes;
};

void bikes_window_create();
void bikes_window_destroy();
Window *bikes_window_get_window();
