#include <pebble.h>
#include "favorites.h"
#include "feedback_window.h"

// Persistent storage layout: a single count key plus one blob key per favorite,
// each holding a struct FavStop (well under the 256-byte per-key limit).
#define PERSIST_FAV_COUNT 1
#define PERSIST_FAV_BASE  100

static struct FavStop favorites[MAX_FAVORITES];
static int fav_count = 0;

// Set whenever the list is mutated; consumed by a window to refresh its view.
static bool fav_changed = false;

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
	for (int i = 0; i < fav_count; ++i) {
		if (strcmp(favorites[i].code, code) == 0) {
			return i;
		}
	}
	return -1;
}

// Rewrites every favorite slot and the count to persistent storage. Called after
// any mutation; the list is short so a full rewrite is simpler than tracking
// dirty slots.
static void favorites_persist(void)
{
	for (int i = 0; i < fav_count; ++i) {
		persist_write_data(PERSIST_FAV_BASE + i, &favorites[i], sizeof(struct FavStop));
	}
	persist_write_int(PERSIST_FAV_COUNT, fav_count);
}

void favorites_load(void)
{
	fav_count = persist_exists(PERSIST_FAV_COUNT) ? persist_read_int(PERSIST_FAV_COUNT) : 0;
	if (fav_count < 0) {
		fav_count = 0;
	}
	if (fav_count > MAX_FAVORITES) {
		fav_count = MAX_FAVORITES;
	}
	for (int i = 0; i < fav_count; ++i) {
		if (persist_exists(PERSIST_FAV_BASE + i)) {
			persist_read_data(PERSIST_FAV_BASE + i, &favorites[i], sizeof(struct FavStop));
		} else {
			// Missing slot: clear it so we never render uninitialized memory.
			favorites[i].code[0] = '\0';
			favorites[i].name[0] = '\0';
			favorites[i].type[0] = '\0';
		}
	}
}

int favorites_count(void)
{
	return fav_count;
}

bool favorites_is_favorite(const char *code)
{
	return find_index(code) != -1;
}

bool favorites_toggle(const char *code, const char *name, const char *type)
{
	int index = find_index(code);
	if (index != -1) {
		// Remove: shift the tail down so the list stays contiguous.
		for (int i = index; i < fav_count - 1; ++i) {
			favorites[i] = favorites[i + 1];
		}
		--fav_count;
		fav_changed = true;
		favorites_persist();
		return false;
	}

	if (fav_count >= MAX_FAVORITES) {
		APP_LOG(APP_LOG_LEVEL_WARNING, "Favorites: list full, cannot add stop %s", code);
		return false;
	}

	struct FavStop *slot = &favorites[fav_count];
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
	++fav_count;
	fav_changed = true;
	favorites_persist();
	return true;
}

bool favorites_consume_changed(void)
{
	bool changed = fav_changed;
	fav_changed = false;
	return changed;
}

bool favorites_get(int index, struct FavStop *out)
{
	if (index < 0 || index >= fav_count) {
		return false;
	}
	*out = favorites[index];
	return true;
}

int favorites_build_codes_csv(char *buf, int size)
{
	int len = 0;
	buf[0] = '\0';
	for (int i = 0; i < fav_count; ++i) {
		const char *sep = (i == 0) ? "" : ",";
		int written = snprintf(buf + len, size - len, "%s%s", sep, favorites[i].code);
		if (written <= 0 || written >= size - len) {
			// Out of room: stop rather than overflow. Should not happen with the
			// MAX_FAVORITES cap and the configured outbox size.
			break;
		}
		len += written;
	}
	return len;
}

static void fav_action_performed(ActionMenu *action_menu, const ActionMenuItem *action, void *context)
{
	// Comparing the before/after state distinguishes a genuine add from a removal,
	// and from an add that was rejected because the list is full.
	bool was_fav = favorites_is_favorite(am_code);
	favorites_toggle(am_code, am_name, am_type);
	bool now_fav = favorites_is_favorite(am_code);

	if (was_fav && !now_fav) {
		strncpy(am_feedback, "Stop removed from favorites", sizeof(am_feedback) - 1);
		vibes_short_pulse();
	} else if (!was_fav && now_fav) {
		strncpy(am_feedback, "Stop added to favorites", sizeof(am_feedback) - 1);
		vibes_short_pulse();
	} else {
		// The list was full, so the stop could not be added. Signal the error with
		// a double vibration.
		strncpy(am_feedback, "Cannot add more favorites", sizeof(am_feedback) - 1);
		vibes_double_pulse();
	}
	am_feedback[sizeof(am_feedback) - 1] = '\0';
}

static void fav_action_menu_did_close(ActionMenu *menu, const ActionMenuItem *performed_action, void *context)
{
	action_menu_hierarchy_destroy(action_menu_get_root_level(menu), NULL, NULL);

	// Only show the confirmation when an action was actually performed (not when
	// the menu was dismissed with the Back button).
	if (performed_action != NULL) {
		feedback_window_show(am_feedback);
	}
}

void favorites_show_action_menu(const char *code, const char *name, const char *type,
                                const char *favorite_label, const char *unfavorite_label)
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

	bool is_fav = favorites_is_favorite(code);

	ActionMenuLevel *root = action_menu_level_create(1);
	action_menu_level_add_action(root, is_fav ? unfavorite_label : favorite_label,
	                             fav_action_performed, NULL);

	ActionMenuConfig config = (ActionMenuConfig) {
		.root_level = root,
		.colors = {
			.background = COLOR_FALLBACK(GColorCobaltBlue, GColorBlack),
			.foreground = GColorWhite,
		},
		.align = ActionMenuAlignCenter,
		.did_close = fav_action_menu_did_close,
	};
	action_menu_open(&config);
}
