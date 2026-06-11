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

// Per-city bus colors, each operator's own brand color (digitransit-ui
// colors.primary) snapped to the Pebble 64-color palette. A few are deliberate
// overrides rather than the nearest snap: Hameenlinna's #F76013 reads as red on the
// watch, and Raasepori's olive #5B7B32 snaps to gray so it is nudged to a green.
// Kouvola is intentionally absent: its brand is black, which reads as no theme, so it
// falls through to the default blue below. The key is the gtfsId feed prefix, the same
// value feed_is() matches on. Cities sharing a color (e.g. Helsinki/Tampere/Lahti) is
// fine: two cities never appear on screen together.
static const struct {
	const char *feed;
	GColor color;
} bus_colors[] = {
	{ "HSL", { .argb = GColorCobaltBlueARGB8 } },        // Helsinki
	{ "tampere", { .argb = GColorCobaltBlueARGB8 } },    // Tampere (Nysse)
	{ "Hameenlinna", { .argb = GColorRedARGB8 } },       // override: brand #F76013
	{ "Joensuu", { .argb = GColorLibertyARGB8 } },
	{ "LINKKI", { .argb = GColorMayGreenARGB8 } },       // Jyvaskyla
	{ "Kotka", { .argb = GColorVividCeruleanARGB8 } },
	{ "Kuopio", { .argb = GColorTiffanyBlueARGB8 } },
	{ "Lahti", { .argb = GColorCobaltBlueARGB8 } },
	{ "Lappeenranta", { .argb = GColorBrilliantRoseARGB8 } },
	{ "Mikkeli", { .argb = GColorPictonBlueARGB8 } },
	{ "OULU", { .argb = GColorFollyARGB8 } },
	{ "Pori", { .argb = GColorOxfordBlueARGB8 } },
	{ "Raasepori", { .argb = GColorArmyGreenARGB8 } },   // override: olive -> keep green
	{ "Rovaniemi", { .argb = GColorMayGreenARGB8 } },
	{ "FOLI", { .argb = GColorChromeYellowARGB8 } },     // Turku (Foli)
	{ "Vaasa", { .argb = GColorDukeBlueARGB8 } },
};

GColor region_mode_color(const char *gtfs_id, char type)
{
	if (type == 'B') {
		// Each city's own bus brand color; un-themed feeds fall back to blue.
		for (unsigned i = 0; i < ARRAY_LENGTH(bus_colors); i++) {
			if (feed_is(gtfs_id, bus_colors[i].feed)) {
				return bus_colors[i].color;
			}
		}
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
