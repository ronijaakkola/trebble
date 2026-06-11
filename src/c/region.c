#include "region.h"
#include <string.h>

// True when gtfs_id belongs to feed, i.e. its prefix up to the ':' equals feed
// (e.g. feed_is("HSL:1234567", "HSL")). A NULL id matches nothing.
static bool feed_is(const char *gtfs_id, const char *feed)
{
	if (!gtfs_id) {
		return false;
	}
	size_t n = strlen(feed);
	return strncmp(gtfs_id, feed, n) == 0 && gtfs_id[n] == ':';
}

GColor region_mode_color(const char *gtfs_id, char type)
{
	if (type == 'B') {
		// Buses are blue across all regions.
		return GColorCobaltBlue;
	}
	if (type == 'T') {
		// Helsinki (HSL) trams are green; other cities keep the default red.
		// GColorJaegerGreen is HSL's tram green (#00985f) snapped to the 64-color
		// palette.
		return feed_is(gtfs_id, "HSL") ? GColorJaegerGreen : GColorRed;
	}
	// Unknown mode: neutral so any letter/text on top stays readable.
	return GColorBlack;
}
