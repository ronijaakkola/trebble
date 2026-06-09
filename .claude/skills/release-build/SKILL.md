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
  captured on every platform's emulator, numbered in navigation order.

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
- `TREBBLE_LOAD_WAIT=9 release.sh` — wait longer for network screens to load
  (see below).

After it finishes, report the bundle path and the screenshot count, and offer to
open a couple of the screenshots so the user can sanity-check them.

## Why it works the way it does

The screenshots are real, live screens, not mockups. The phone-side JS
(`src/pkjs/app.js`) detects the emulator and substitutes a fixed Tampere GPS
location (`debugLocation`), then queries the live Digitransit API. So booting the
emulator and walking the menus yields genuine stop and departure data.

The navigation the script performs on each platform:

1. **Splash** — captured immediately after install, before it auto-dismisses
   (~1.5s).
2. **Home** — the two-row menu (Nearby stops / Pinned stops).
3. **Nearby stops** — `select` the top row; waits for the API, then captures the
   stop list.
4. **Departures** — `select` the first stop; waits for the API, then captures the
   departures list.
5. **Pinned stops** — `back` to Home, `down` to the second row, `select`.

If a screen comes up blank or still on a loading state, the API round trip was
slower than the wait. Re-run with a larger `TREBBLE_LOAD_WAIT` rather than
editing the script. Screens 3 and 4 are the data-dependent ones; splash, home,
and pinned are instant.

## Things to keep in mind

- Run from the repo root — the script locates everything relative to
  `package.json` and reads the version and platform list from it, so new
  platforms or a version bump are picked up automatically.
- The script kills and relaunches a fresh emulator per platform so navigation
  always starts from the splash screen. Expect it to take a couple of minutes
  across all five.
- `release/` is gitignored. If a release run reports it isn't, add `release/` to
  `.gitignore` — these are regenerable artifacts and shouldn't be committed.
- This does **not** publish anything. Uploading the `.pbw` to the app store
  (`pebble publish` / the dashboard) is a separate, deliberate step the user
  drives.
