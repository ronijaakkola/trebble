#pragma once
#include <pebble.h>

// Maximum number of stops the user can pin. Kept in step with the number of
// stops/lines the rest of the app handles so the pinned list fits in memory.
#define MAX_PINS 10

// A pinned stop. The code (Digitransit gtfsId) is the identity; name and type
// are cached so the pinned list can render without a round-trip, and so a pin
// created from the departures screen still shows a sensible label.
struct PinnedStop {
	char code[20];
	char name[30];
	char type[2]; // 'B', 'T' or '\0' (unknown), matching StopInfo.type
};

// Loads the persisted pins into memory. Call once at startup.
void pins_load(void);

// Number of stops currently pinned.
int pins_count(void);

// True if the stop with the given code is pinned.
bool pins_is_pinned(const char *code);

// Pins the stop if it is not pinned, unpins it if it is. Returns the new state
// (true = now pinned). No-op (returns false) if pinning would exceed MAX_PINS.
// The change is persisted immediately.
bool pins_toggle(const char *code, const char *name, const char *type);

// Copies the pinned stop at the given index into out. Returns false if out of range.
bool pins_get(int index, struct PinnedStop *out);

// Returns whether the pins changed since this was last called, clearing the flag
// in the process. Lets a window that is already showing pins refresh itself when
// a stop was pinned/unpinned while it was covered by another window.
bool pins_consume_changed(void);

// Writes a comma-separated list of all pinned codes into buf. Returns the written
// length (excluding the terminator), or 0 when there are no pins.
int pins_build_codes_csv(char *buf, int size);

// Opens a single-action ActionMenu over the current window to pin/unpin the given
// stop. The shown label is pin_label when the stop is not yet pinned, and
// unpin_label when it already is.
void pins_show_action_menu(const char *code, const char *name, const char *type,
                           const char *pin_label, const char *unpin_label);
