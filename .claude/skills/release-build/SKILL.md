---
name: release-build
description: >
  Prepare Trebble release artifacts: build the multi-platform Pebble .pbw bundle
  and capture per-platform, per-screen screenshots for the app store / README.
  Use when the user wants to cut a release, prep release files, produce build
  artifacts, regenerate app-store screenshots, or "get everything ready to
  publish/ship." Triggers on requests like "build the release," "prepare release
  files," "make the release assets," "take screenshots of all the screens," or
  "package the app for all platforms" — even when the word "release" isn't used
  but the intent is producing distributable build output and marketing
  screenshots. Specific to this Trebble Pebble watch-app repo.
---

# Trebble release build

Produces two things, both under the gitignored `release/` folder:

- **`release/builds/trebble-<version>.pbw`** — the installable bundle. A Pebble
  `.pbw` is a single fat bundle containing all target platforms (aplite, basalt,
  chalk, diorite, emery), so this one file *is* the all-platform build and the
  artifact you upload to the app store.
- **`release/screenshots/<platform>/NN-<screen>.png`** — every screen of the app
  captured on every platform's emulator, numbered in navigation order, with
  **consistent fixed data** every run (see "Screenshot data" below; aplite is the
  one exception — it falls back to live data, see "aplite is a special case"):
  - `01-splash` — the wordmark splash.
  - `02-home` — the main menu; "Pinned stops" shows **4 stops**.
  - `03-nearby-stops` — nearby list: **bus, metro, bus** (in that order).
  - `04-pins` — pinned list: **4 pins, 2 buses + 1 tram + 1 metro**.
  - `05-departures` — a **tram** stop's departures; the first two lines use the
    minute display (**"now"** and **"3 min"**).

## How to run it

The whole job is deterministic, so run the bundled script from the project root:

```bash
.claude/skills/release-build/scripts/release.sh
```

Useful variants:

- `release.sh --no-shots` — build the versioned bundle only, skip the (slow)
  emulator screenshot pass.
- `release.sh basalt chalk` — limit the screenshot pass to specific platforms
  (the bundle is always built for all of them).
- `TREBBLE_LOAD_WAIT=9 release.sh` — wait longer for a data screen to render
  (see below).
- `TREBBLE_SHOT_TIME=08:45:00 release.sh` — pin the status-bar clock to a
  different time (default `10:09:00`).

After it finishes, report the bundle path and the screenshot count, and offer to
open a couple of the screenshots so the user can sanity-check them.

## Screenshot data (the consistent-data mechanism)

The screenshots are real screens, but the **data is fixed fixtures**, not the
live API — so every release showcases the same stops, departures and pins. This
is what makes the four marketing shots reproducible.

How it is wired (all the screenshot-only code is gated, so the **shipped artifact
carries none of it** — important for aplite's tiny heap):

- **`TREBBLE_SCREENSHOT=1`** at build time defines `SCREENSHOT_MODE`
  (`wscript` adds `-DSCREENSHOT_MODE`). The release script builds the artifact
  normally **and** a second, screenshot-only bundle with this flag; the emulator
  is fed the second bundle.
- **Watch side** (gated by `SCREENSHOT_MODE`): seeds four fixture pins
  (`pins_seed_fixtures` in `src/c/pins.c`, called from `init`) so the home-menu
  count and the pinned list agree, and tags each request to the phone with a
  `screenshotMode` flag.
- **Phone side** (`src/pkjs/app.js` + `src/pkjs/fixtures.js`): on seeing that
  flag it latches "screenshot mode" on and serves the deterministic Helsinki
  fixtures (nearby stops, departures, pins, city header) instead of calling
  Digitransit. In a normal build the watch never sends the flag, so this path is
  dead and the live API is used.
- **The clock** is pinned with `pebble emu-set-time` (and forced to 24h) so the
  status bar is identical across shots; the fixture departure countdowns are
  baked in (they assume ~10:09), so they don't depend on the wall clock at all.

To change *what* the screenshots show (stop names, line numbers, the tram's
departures, the pin set), edit the fixtures in `src/pkjs/fixtures.js`. If you
change the pin set, keep `pins_seed_fixtures` in `src/c/pins.c` in step with it
(same number of pins, same codes) so the home-menu count matches the list.

## The navigation the script performs

Per platform it kills any emulator, installs the **screenshot bundle**, pins the
clock, then walks the menus:

1. **`01-splash`** — captured immediately after install, before it auto-dismisses.
2. **`02-home`** — the two-row main menu; "Pinned stops" reads "4 stops".
3. **`03-nearby-stops`** — `select` the top row → the fixture stops (bus, metro,
   bus).
4. **`04-pins`** — `back` to Home, `down`, `select` → the fixture pinned list
   (2 buses, 1 tram, 1 metro), tram first.
5. **`05-departures`** — `select` the first pin (the tram) → its fixture
   departures, first two lines on the minute display ("now", "3 min").

If a screen comes up still on "Loading..", the watch↔phone round trip was slower
than the wait; re-run with a larger `TREBBLE_LOAD_WAIT` rather than editing the
script. To shoot at a different clock time, set `TREBBLE_SHOT_TIME=HH:MM:SS`.

### aplite is a special case (live-data fallback)

aplite's app heap is only ~3.5KB and it faults on launch if the fixture build's
extra ~400 bytes are added, so **aplite does not use the fixtures**. The script
detects this (`NOFIX_PLATFORMS="aplite"`) and instead installs the shippable
bundle and walks the menus on **live debug-location data** — the original
approach. So on aplite the shots are the regular live screens (`01-splash`,
`02-home`, `03-nearby-stops`, `04-departures`, `05-pinned-stops`), not the
controlled fixture set, and their data is whatever Digitransit returns. Every
other platform (basalt, chalk, diorite, emery) gets the deterministic fixtures.

If a future platform turns out to be too tight for the fixture build, add it to
`NOFIX_PLATFORMS` in the script and it will use the same fallback.

## Things to keep in mind

- Run from the repo root — the script locates everything relative to
  `package.json` and reads the version and platform list from it, so new
  platforms or a version bump are picked up automatically.
- The script kills and relaunches a fresh emulator per platform so navigation
  always starts from the splash screen. Expect it to take a couple of minutes
  across all five.
- The run builds **twice** (the artifact, then the fixture bundle) with a
  `pebble clean` before each, because `SCREENSHOT_MODE` and the `screenshotMode`
  message key only take after a clean. The fixture bundle lands at
  `release/.trebble-<version>-screenshot.pbw` — it is for the emulator only,
  **never** upload it. The artifact uploaded to the store is the one in
  `release/builds/`.
- `release/` is gitignored. If a release run reports it isn't, add `release/` to
  `.gitignore` — these are regenerable artifacts and shouldn't be committed.
- This does **not** publish anything. Uploading the `.pbw` to the app store
  (`pebble publish` / the dashboard) is a separate, deliberate step the user
  drives.
