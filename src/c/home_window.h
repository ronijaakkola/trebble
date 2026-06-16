#pragma once

#define HOME_HL_COLOR GColorLightGray

void home_window_create();
void home_window_destroy();
Window *home_window_get_window();

// Starts the city lookup early (during the splash screen) so the menu header is
// usually already resolved by the time the home window is shown, rather than
// visibly swapping from "Loading.." to the city after the menu appears. Reads the
// last known city, registers the city inbox handler, and sends the request.
void home_window_start_location_lookup();
