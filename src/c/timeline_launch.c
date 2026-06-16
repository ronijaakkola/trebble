#include <pebble.h>
#include "timeline_launch.h"

// The whole table is timeline-only; aplite has no timeline (and a tiny heap), so
// it compiles to nothing there and is never referenced.
#ifndef PBL_PLATFORM_APLITE

// Persistent layout: a count, the next code to hand out, then one blob per entry.
// Kept well clear of the pins (1, 100+) and city (50) keys.
#define PERSIST_TL_COUNT 300
#define PERSIST_TL_NEXT  301
#define PERSIST_TL_BASE  310

// Bounded to a handful of recently pinned stops; the oldest is evicted when full.
#define TL_MAX 8

struct TLEntry {
	uint32_t code;     // the launch code handed to the pin action
	char stop[20];     // StopInfo.code (gtfsId)
	char name[30];
	char type[2];
};

static struct TLEntry entries[TL_MAX];
static int tl_count = 0;
// Monotonic source of launch codes. 0 is reserved for "no specific stop", so
// codes start at 1 and never repeat (even across evictions) within persistence.
static uint32_t tl_next = 1;
static bool loaded = false;

static void tl_load(void)
{
	if (loaded) {
		return;
	}
	loaded = true;

	tl_count = persist_exists(PERSIST_TL_COUNT) ? persist_read_int(PERSIST_TL_COUNT) : 0;
	if (tl_count < 0) {
		tl_count = 0;
	}
	if (tl_count > TL_MAX) {
		tl_count = TL_MAX;
	}
	tl_next = persist_exists(PERSIST_TL_NEXT) ? (uint32_t)persist_read_int(PERSIST_TL_NEXT) : 1;
	if (tl_next == 0) {
		tl_next = 1;
	}
	for (int i = 0; i < tl_count; ++i) {
		if (persist_exists(PERSIST_TL_BASE + i)) {
			persist_read_data(PERSIST_TL_BASE + i, &entries[i], sizeof(struct TLEntry));
		} else {
			entries[i].code = 0;
			entries[i].stop[0] = '\0';
		}
	}
}

static void tl_persist(void)
{
	for (int i = 0; i < tl_count; ++i) {
		persist_write_data(PERSIST_TL_BASE + i, &entries[i], sizeof(struct TLEntry));
	}
	persist_write_int(PERSIST_TL_COUNT, tl_count);
	persist_write_int(PERSIST_TL_NEXT, (int)tl_next);
}

uint32_t timeline_launch_register(const char *code, const char *name, const char *type)
{
	tl_load();

	// Same stop already registered: reuse its code so its pins stay valid and the
	// table does not fill with duplicates of one stop.
	for (int i = 0; i < tl_count; ++i) {
		if (strcmp(entries[i].stop, code) == 0) {
			return entries[i].code;
		}
	}

	// Evict the oldest entry (slot 0) when full, keeping the list contiguous.
	if (tl_count == TL_MAX) {
		for (int i = 0; i < TL_MAX - 1; ++i) {
			entries[i] = entries[i + 1];
		}
		--tl_count;
	}

	struct TLEntry *e = &entries[tl_count++];
	e->code = tl_next++;
	strncpy(e->stop, code, sizeof(e->stop) - 1);
	e->stop[sizeof(e->stop) - 1] = '\0';
	strncpy(e->name, name, sizeof(e->name) - 1);
	e->name[sizeof(e->name) - 1] = '\0';
	if (type && type[0]) {
		e->type[0] = type[0];
		e->type[1] = '\0';
	} else {
		e->type[0] = '\0';
	}
	tl_persist();
	return e->code;
}

bool timeline_launch_lookup(uint32_t launch_code, char *out_code, char *out_name, char *out_type)
{
	if (launch_code == 0) {
		return false;
	}
	tl_load();
	for (int i = 0; i < tl_count; ++i) {
		if (entries[i].code == launch_code) {
			strcpy(out_code, entries[i].stop);
			strcpy(out_name, entries[i].name);
			out_type[0] = entries[i].type[0];
			out_type[1] = '\0';
			return true;
		}
	}
	return false;
}

#endif // !PBL_PLATFORM_APLITE
