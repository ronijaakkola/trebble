#pragma once
#include <pebble.h>

// Maps the launch code carried by a timeline pin's "Open app" action back to the
// stop the pin was created for, so launching the app from that action can open
// the stop's departures directly. The table is persisted (a pin can be opened
// long after the app was last run) and bounded; the oldest entry is evicted once
// full. Compiled only where the timeline exists (not aplite).

// Registers a stop and returns a stable, non-zero launch code for it (reusing the
// existing code if the same stop is already registered). Pass the returned code
// as the pin action's launchCode.
uint32_t timeline_launch_register(const char *code, const char *name, const char *type);

// Resolves a launch code to its stop, copying into the out buffers (sized as in
// struct StopInfo: code[20], name[30], type[2]). Returns false for code 0 or an
// unknown/evicted code.
bool timeline_launch_lookup(uint32_t launch_code, char *out_code, char *out_name, char *out_type);
