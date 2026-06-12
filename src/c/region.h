#pragma once
#include <pebble.h>

// Returns the badge/bar color for a transit mode at a given stop, keyed off the
// stop's Digitransit feed (the prefix of its gtfsId, e.g. "HSL:..."). Buses use each
// city's own brand color (see the bus_colors table in region.c), falling back to blue
// for un-themed feeds; trams are green in Helsinki (HSL) and red elsewhere. Unknown
// modes get a neutral black. The returned value is the color-watch color; callers
// wrap it in COLOR_FALLBACK where a black-and-white fallback is wanted.
GColor region_mode_color(const char *gtfs_id, char type);

// Maps a Digitransit vehicle mode string to a single-letter type indicator:
// "BUS" -> 'B', "TRAM" -> 'T', "SUBWAY" -> 'M'. Any other (unsupported) mode
// yields an empty string. `out` must have room for two chars; it is always
// NUL-terminated. Shared by every window so a new mode is added in one place.
void region_mode_to_type_letter(const char *mode, char *out);
