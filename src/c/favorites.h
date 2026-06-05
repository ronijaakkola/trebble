#pragma once
#include <pebble.h>

// Maximum number of stops the user can favorite. Kept in step with the number of
// stops/lines the rest of the app handles so the favorites list fits in memory.
#define MAX_FAVORITES 10

// A favorited stop. The code (Digitransit gtfsId) is the identity; name and type
// are cached so the favorites list can render without a round-trip, and so a
// favorite created from the departures screen still shows a sensible label.
struct FavStop {
	char code[20];
	char name[30];
	char type[2]; // 'B', 'T' or '\0' (unknown), matching StopInfo.type
};

// Loads the persisted favorites into memory. Call once at startup.
void favorites_load(void);

// Number of stops currently favorited.
int favorites_count(void);

// True if the stop with the given code is favorited.
bool favorites_is_favorite(const char *code);

// Adds the stop if it is not favorited, removes it if it is. Returns the new
// state (true = now favorited). No-op (returns false) if adding would exceed
// MAX_FAVORITES. The change is persisted immediately.
bool favorites_toggle(const char *code, const char *name, const char *type);

// Copies the favorite at the given index into out. Returns false if out of range.
bool favorites_get(int index, struct FavStop *out);

// Returns whether the favorites changed since this was last called, clearing the
// flag in the process. Lets a window that is already showing favorites refresh
// itself when a stop was added/removed while it was covered by another window.
bool favorites_consume_changed(void);

// Writes a comma-separated list of all favorite codes into buf. Returns the
// written length (excluding the terminator), or 0 when there are no favorites.
int favorites_build_codes_csv(char *buf, int size);

// Opens a single-action ActionMenu over the current window to favorite/unfavorite
// the given stop. The shown label is favorite_label when the stop is not yet a
// favorite, and unfavorite_label when it already is.
void favorites_show_action_menu(const char *code, const char *name, const char *type,
                                const char *favorite_label, const char *unfavorite_label);
