#include <pebble.h>
#include "pins.h"
#include "feedback_window.h"

// Persistent storage layout: a single count key plus one blob key per pin, each
// holding a struct PinnedStop (well under the 256-byte per-key limit).
#define PERSIST_PIN_COUNT 1
#define PERSIST_PIN_BASE  100

static struct PinnedStop pins[MAX_PINS];
static int pin_count = 0;

// Set whenever the list is mutated; consumed by a window to refresh its view.
static bool pin_changed = false;

// Stop targeted by the currently open action menu. Held here so the action
// callback (which only receives the menu item) knows what to toggle.
static char am_code[20];
static char am_name[30];
static char am_type[2];

// Confirmation message for the most recently performed action, shown once the
// action menu finishes closing.
static char am_feedback[40];

static int find_index(const char *code)
{
	for (int i = 0; i < pin_count; ++i) {
		if (strcmp(pins[i].code, code) == 0) {
			return i;
		}
	}
	return -1;
}

// Rewrites every pin slot and the count to persistent storage. Called after any
// mutation; the list is short so a full rewrite is simpler than tracking dirty
// slots.
static void pins_persist(void)
{
	for (int i = 0; i < pin_count; ++i) {
		persist_write_data(PERSIST_PIN_BASE + i, &pins[i], sizeof(struct PinnedStop));
	}
	persist_write_int(PERSIST_PIN_COUNT, pin_count);
}

void pins_load(void)
{
	pin_count = persist_exists(PERSIST_PIN_COUNT) ? persist_read_int(PERSIST_PIN_COUNT) : 0;
	if (pin_count < 0) {
		pin_count = 0;
	}
	if (pin_count > MAX_PINS) {
		pin_count = MAX_PINS;
	}
	for (int i = 0; i < pin_count; ++i) {
		if (persist_exists(PERSIST_PIN_BASE + i)) {
			persist_read_data(PERSIST_PIN_BASE + i, &pins[i], sizeof(struct PinnedStop));
		} else {
			// Missing slot: clear it so we never render uninitialized memory.
			pins[i].code[0] = '\0';
			pins[i].name[0] = '\0';
			pins[i].type[0] = '\0';
		}
	}
}

int pins_count(void)
{
	return pin_count;
}

#ifdef SCREENSHOT_MODE
void pins_seed_fixtures(void)
{
	// Only the COUNT matters for the screenshots: the home-menu subtitle reads
	// pins_count(), and the pinned-stops window needs a non-empty list to fire its
	// request — whose actual contents the phone ignores in screenshot mode (it
	// returns the four PINNED_STOPS from fixtures.js regardless). So seed four
	// minimal stub pins rather than real codes/names; this keeps the aplite footprint
	// tiny (its heap is ~3.5KB and overflows easily). Not persisted: it is reseeded
	// on every launch of a screenshot build, so it is deterministic anyway.
	for (int i = 0; i < 4; ++i) {
		pins[i].code[0] = (char)('1' + i);
		pins[i].code[1] = '\0';
		pins[i].name[0] = '\0';
		pins[i].type[0] = '\0';
	}
	pin_count = 4;
}
#endif

bool pins_is_pinned(const char *code)
{
	return find_index(code) != -1;
}

bool pins_toggle(const char *code, const char *name, const char *type)
{
	int index = find_index(code);
	if (index != -1) {
		// Unpin: shift the tail down so the list stays contiguous.
		for (int i = index; i < pin_count - 1; ++i) {
			pins[i] = pins[i + 1];
		}
		--pin_count;
		pin_changed = true;
		pins_persist();
		return false;
	}

	if (pin_count >= MAX_PINS) {
		APP_LOG(APP_LOG_LEVEL_WARNING, "Pins: list full, cannot pin stop %s", code);
		return false;
	}

	struct PinnedStop *slot = &pins[pin_count];
	strncpy(slot->code, code, sizeof(slot->code) - 1);
	slot->code[sizeof(slot->code) - 1] = '\0';
	strncpy(slot->name, name, sizeof(slot->name) - 1);
	slot->name[sizeof(slot->name) - 1] = '\0';
	if (type && type[0]) {
		slot->type[0] = type[0];
		slot->type[1] = '\0';
	} else {
		slot->type[0] = '\0';
	}
	++pin_count;
	pin_changed = true;
	pins_persist();
	return true;
}

bool pins_consume_changed(void)
{
	bool changed = pin_changed;
	pin_changed = false;
	return changed;
}

bool pins_get(int index, struct PinnedStop *out)
{
	if (index < 0 || index >= pin_count) {
		return false;
	}
	*out = pins[index];
	return true;
}

int pins_build_codes_csv(char *buf, int size)
{
	int len = 0;
	buf[0] = '\0';
	for (int i = 0; i < pin_count; ++i) {
		const char *sep = (i == 0) ? "" : ",";
		int written = snprintf(buf + len, size - len, "%s%s", sep, pins[i].code);
		if (written <= 0 || written >= size - len) {
			// Out of room: stop rather than overflow. Should not happen with the
			// MAX_PINS cap and the configured outbox size.
			break;
		}
		len += written;
	}
	return len;
}

// Toggles the pin for the stop held in am_code/am_name/am_type and records a
// confirmation message in am_feedback, vibrating to match the outcome.
static void pins_perform_toggle(void)
{
	// Comparing the before/after state distinguishes a genuine pin from an unpin,
	// and from a pin that was rejected because the list is full.
	bool was_pinned = pins_is_pinned(am_code);
	pins_toggle(am_code, am_name, am_type);
	bool now_pinned = pins_is_pinned(am_code);

	if (was_pinned && !now_pinned) {
		strncpy(am_feedback, "Stop unpinned", sizeof(am_feedback) - 1);
		vibes_short_pulse();
	} else if (!was_pinned && now_pinned) {
		strncpy(am_feedback, "Stop pinned", sizeof(am_feedback) - 1);
		vibes_short_pulse();
	} else {
		// The list was full, so the stop could not be pinned. Signal the error with
		// a double vibration.
		strncpy(am_feedback, "Cannot pin more stops", sizeof(am_feedback) - 1);
		vibes_double_pulse();
	}
	am_feedback[sizeof(am_feedback) - 1] = '\0';
}

#ifndef PBL_PLATFORM_APLITE
static void pin_action_performed(ActionMenu *action_menu, const ActionMenuItem *action, void *context)
{
	pins_perform_toggle();
}

static void pin_action_menu_did_close(ActionMenu *menu, const ActionMenuItem *performed_action, void *context)
{
	action_menu_hierarchy_destroy(action_menu_get_root_level(menu), NULL, NULL);

	// Only show the confirmation when an action was actually performed (not when
	// the menu was dismissed with the Back button).
	if (performed_action != NULL) {
		feedback_window_show(am_feedback);
	}
}
#endif

void pins_show_action_menu(const char *code, const char *name, const char *type,
                           const char *pin_label, const char *unpin_label)
{
	strncpy(am_code, code, sizeof(am_code) - 1);
	am_code[sizeof(am_code) - 1] = '\0';
	strncpy(am_name, name, sizeof(am_name) - 1);
	am_name[sizeof(am_name) - 1] = '\0';
	if (type && type[0]) {
		am_type[0] = type[0];
		am_type[1] = '\0';
	} else {
		am_type[0] = '\0';
	}

#ifdef PBL_PLATFORM_APLITE
	// Aplite's heap is too small to open the ActionMenu window on top of the list
	// it is invoked from (it faults trying to allocate). Pin/unpin directly and
	// confirm with the lightweight feedback toast instead. The pin/unpin labels are
	// unused here; the action is implied by the stop's current state.
	(void) pin_label;
	(void) unpin_label;
	pins_perform_toggle();
	feedback_window_show(am_feedback);
	return;
#else
	bool is_pinned = pins_is_pinned(code);

	ActionMenuLevel *root = action_menu_level_create(1);
	action_menu_level_add_action(root, is_pinned ? unpin_label : pin_label,
	                             pin_action_performed, NULL);

	ActionMenuConfig config = (ActionMenuConfig) {
		.root_level = root,
		.colors = {
			// Match the stop type: red for tram and metro, blue for bus (and
			// unknown). B&W watches fall back to black, where the color carries no
			// meaning.
			.background = COLOR_FALLBACK((am_type[0] == 'T' || am_type[0] == 'M') ? GColorRed : GColorCobaltBlue, GColorBlack),
			.foreground = GColorWhite,
		},
		.align = ActionMenuAlignCenter,
		.did_close = pin_action_menu_did_close,
	};
	action_menu_open(&config);
#endif
}
