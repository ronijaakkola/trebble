#!/usr/bin/env bash
#
# Prepare Trebble release artifacts:
#   1. Clean build of the multi-platform .pbw bundle (the shippable artifact).
#   2. A versioned copy of the bundle into release/builds/.
#   3. Screenshots of every screen on every target platform into
#      release/screenshots/<platform>/, captured from a SEPARATE screenshot build
#      that serves deterministic Helsinki fixtures (see "Screenshot data" below).
#
# Everything lands under release/, which is gitignored.
#
# Usage:
#   scripts/release.sh                 # build + screenshot all platforms
#   scripts/release.sh --no-shots      # build the bundle only, skip screenshots
#   scripts/release.sh basalt chalk    # build + screenshot only these platforms
#   scripts/release.sh --tag           # also commit the bump + changelog and tag v<version>
#
# Run from the project root (where package.json lives).

set -euo pipefail

# --- locate project root ---------------------------------------------------
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$ROOT"

if [[ ! -f package.json ]]; then
  echo "error: package.json not found in $ROOT — run this from the Trebble project root" >&2
  exit 1
fi

# --- read version + target platforms from package.json ---------------------
VERSION="$(python3 -c 'import json;print(json.load(open("package.json"))["version"])')"
ALL_PLATFORMS="$(python3 -c 'import json;print(" ".join(json.load(open("package.json"))["pebble"]["targetPlatforms"]))')"

# --- parse args ------------------------------------------------------------
TAKE_SHOTS=1
TAG_RELEASE=0
PLATFORMS=()
for arg in "$@"; do
  case "$arg" in
    --no-shots) TAKE_SHOTS=0 ;;
    --tag) TAG_RELEASE=1 ;;
    -*) echo "error: unknown flag $arg" >&2; exit 1 ;;
    *) PLATFORMS+=("$arg") ;;
  esac
done
if [[ ${#PLATFORMS[@]} -eq 0 ]]; then
  read -r -a PLATFORMS <<< "$ALL_PLATFORMS"
fi

# How long to wait (seconds) after a button press for a data screen to render.
# In screenshot mode the data is local fixtures (no network), so this only needs
# to cover the watch<->phone message round trip and rendering. Bump it if a screen
# comes up still on "Loading..".
LOAD_WAIT="${TREBBLE_LOAD_WAIT:-5}"

# Wall-clock time the watch status bar is pinned to for every screenshot, so the
# clock is identical across shots and platforms. The fixture departure countdowns
# ("now", "3 min", …) are baked in and assume ~10:09, so keep this in step with
# them if you change it. HH:MM:SS, local watch time.
SHOT_TIME="${TREBBLE_SHOT_TIME:-10:09:00}"

BUILD_OUT="release/builds"
SHOTS_OUT="release/screenshots"

# `pebble build` names the bundle after the project directory (e.g.
# build/trebble.pbw), so resolve it rather than hardcoding the name.
built_pbw() {
  local pbw
  pbw="$(ls -1 build/*.pbw 2>/dev/null | head -1)"
  if [[ -z "$pbw" ]]; then
    echo "error: no .pbw found in build/ after build" >&2
    exit 1
  fi
  echo "$pbw"
}
# Internal, not-for-upload bundle that carries the screenshot fixtures. Kept under
# release/ (gitignored) and clearly named so it is never mistaken for the artifact.
SHOT_PBW="release/.trebble-$VERSION-screenshot.pbw"

echo "==> Trebble release v$VERSION"
echo "    platforms: ${PLATFORMS[*]}"
echo

# --- 1. clean build (the shippable artifact) -------------------------------
# A plain build: no SCREENSHOT_MODE, so it carries none of the fixture code.
echo "==> Building release bundle (clean)…"
pebble clean >/dev/null 2>&1 || true
pebble build 2>&1 | tail -3

mkdir -p "$BUILD_OUT"
PBW="$BUILD_OUT/trebble-$VERSION.pbw"
cp "$(built_pbw)" "$PBW"
echo "==> Bundle: $PBW"
echo

# --- 1b. changelog ---------------------------------------------------------
# Compile a CHANGELOG.md entry for this release: every commit since the previous
# release, bounded by the most recent git tag reachable from HEAD (releases are
# tagged v<version>, e.g. by `--tag` below). This writes the technical changelog,
# one bullet per commit, and prints the same commit list so the skill can curate
# the short, user-facing store changelog from it (see SKILL.md). Runs even with
# --no-shots.
echo "==> Compiling changelog for v$VERSION…"

SINCE_TAG="$(git describe --tags --abbrev=0 2>/dev/null || true)"
if [[ -n "$SINCE_TAG" ]]; then
  RANGE="$SINCE_TAG..HEAD"
  echo "    Range: since $SINCE_TAG"
else
  # No tags yet (first tagged release). Bootstrap from the previous *version
  # bump* instead — the most recent commit that set a version DIFFERENT from the
  # current one — so this run still produces a sensible changelog. Once this
  # release is tagged (--tag, or manually), later runs use the tag above.
  SINCE="$(python3 - "$VERSION" <<'PY'
import json, subprocess, sys
version = sys.argv[1]
try:
    commits = subprocess.check_output(
        ["git", "log", "--format=%H", "-G", '"version"[[:space:]]*:', "--", "package.json"],
        text=True,
    ).split()
except subprocess.CalledProcessError:
    commits = []
since = ""
for h in commits:  # newest -> oldest; first commit whose version != current is the boundary
    try:
        v = json.loads(subprocess.check_output(["git", "show", f"{h}:package.json"], text=True))["version"]
    except Exception:
        continue
    if v != version:
        since = h
        break
print(since)
PY
)"
  if [[ -n "$SINCE" ]]; then
    RANGE="$SINCE..HEAD"
    echo "    Range: no tags yet — since previous version bump ${SINCE:0:7}"
  else
    RANGE="HEAD"   # no tags and no earlier version — take the whole history
    echo "    Range: no tags yet — whole history"
  fi
fi

# Raw commit subjects for this release (newest first, merges excluded).
COMMITS="$(git log --no-merges --format='- %s (%h)' $RANGE)"

if [[ -z "$COMMITS" ]]; then
  echo "    No commits since the last version bump — skipping changelog."
else
  DATE="$(date +%Y-%m-%d)"
  # Prepend (or refresh) the v$VERSION section at the top of CHANGELOG.md so a
  # re-run regenerates it in place rather than duplicating it.
  COMMITS="$COMMITS" VERSION="$VERSION" DATE="$DATE" python3 - <<'PY'
import os, re
path = "CHANGELOG.md"
version, date, commits = os.environ["VERSION"], os.environ["DATE"], os.environ["COMMITS"]
section = f"## v{version} — {date}\n\n{commits}\n"
existing = open(path).read() if os.path.exists(path) else ""
# Drop any existing section for this version, then re-add it at the top.
existing = re.sub(rf"(?ms)^## v{re.escape(version)} .*?(?=^## |\Z)", "", existing)
if existing.startswith("# Changelog"):
    existing = existing[len("# Changelog"):]
existing = existing.lstrip("\n")
out = "# Changelog\n\n" + section + (("\n" + existing) if existing.strip() else "")
open(path, "w").write(out.rstrip("\n") + "\n")
print(f"    Wrote {path} — v{version} ({commits.count(chr(10)) + 1} commits)")
PY
  echo
  echo "    --- commits in this release (curate the user-facing store list from these) ---"
  echo "$COMMITS" | sed 's/^/    /'
  echo
fi

# Mark this release in git: commit the version bump + changelog and tag
# v<version>. Only runs with --tag; otherwise the run makes no git changes and the
# final summary prints the equivalent manual commands. The tag is local — pushing
# (and publishing the .pbw) stays a deliberate, separate step. The next release's
# changelog is bounded by this tag.
finalize_release() {
  [[ $TAG_RELEASE -eq 1 ]] || return 0
  local tag="v$VERSION"
  echo "==> Finalizing release $tag (commit + tag)…"
  if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
    echo "    WARN: tag $tag already exists — leaving git untouched."
    echo
    return 0
  fi
  # Stage only the release files, so unrelated working-tree changes are untouched.
  git add package.json CHANGELOG.md 2>/dev/null || true
  if git diff --cached --quiet; then
    echo "    Nothing to commit (package.json + CHANGELOG.md already committed)."
  else
    git commit -m "Release $tag" >/dev/null
    echo "    Committed version bump + changelog as \"Release $tag\"."
  fi
  git tag "$tag"
  echo "    Tagged $tag (local). Push when publishing: git push && git push origin $tag"
  echo
}

if [[ $TAKE_SHOTS -eq 0 ]]; then
  echo "==> Skipping screenshots (--no-shots)."
  finalize_release
  echo "==> Done."
  exit 0
fi

# Platforms whose heap is too small for the fixture build (it adds ~400 bytes and
# aplite faults on launch at that margin). These fall back to the original
# live-data approach below instead of the fixtures.
NOFIX_PLATFORMS="aplite"
uses_fixtures() { # <platform> -> 0 if it should use the fixture build
  case " $NOFIX_PLATFORMS " in *" $1 "*) return 1 ;; *) return 0 ;; esac
}

# Does any requested platform actually use the fixture build?
ANY_FIXTURE=0
for P in "${PLATFORMS[@]}"; do
  if uses_fixtures "$P"; then ANY_FIXTURE=1; break; fi
done

# --- 2. screenshot build (deterministic fixtures) --------------------------
# A SECOND build with TREBBLE_SCREENSHOT=1, which defines SCREENSHOT_MODE: the
# watch seeds four fixture pins and tells the phone JS to serve fixed Helsinki
# data instead of the live API. A clean is required so the new define (and the
# screenshotMode message key) actually take. This bundle is only ever installed in
# the emulator below — it is never the artifact uploaded to the store. Skipped if
# every requested platform is on the live-data fallback.
if [[ $ANY_FIXTURE -eq 1 ]]; then
  echo "==> Building screenshot bundle (TREBBLE_SCREENSHOT=1, clean)…"
  TREBBLE_SCREENSHOT=1 pebble clean >/dev/null 2>&1 || true
  TREBBLE_SCREENSHOT=1 pebble build 2>&1 | tail -3
  cp "$(built_pbw)" "$SHOT_PBW"
  echo "==> Screenshot bundle: $SHOT_PBW"
  echo
fi

# --- 3. screenshots per platform -------------------------------------------
# Emulator interactions are inherently flaky (cold boots, transient timeouts), and
# the artifact is already built by this point, so don't let one bad step abort the
# whole run. Tolerate failures, warn, and keep going.
set +e
FAILS=0

# Helper: capture the current emulator screen, retrying once.
shot() { # <platform> <filename>
  pebble screenshot --emulator "$1" --no-open "$2" >/dev/null 2>&1 \
    || pebble screenshot --emulator "$1" --no-open "$2" >/dev/null 2>&1
  if [[ -f "$2" ]]; then
    echo "    saved $2"
  else
    echo "    WARN: failed to capture $2" >&2
    FAILS=$((FAILS + 1))
  fi
}
press() { # <platform> <button>
  pebble emu-button --emulator "$1" click "$2" >/dev/null 2>&1
}
# Capture a data screen with the status-bar clock pinned. The connected phone
# (pypkjs) re-syncs the watch to host time as the app runs, so the clock has to be
# re-set right before each shot (it redraws promptly) rather than once up front.
# A short settle lets the status bar repaint at the new time before we capture.
timed_shot() { # <platform> <filename>
  pebble emu-set-time --emulator "$1" "$SHOT_TIME" >/dev/null 2>&1
  sleep 2
  shot "$1" "$2"
}

for P in "${PLATFORMS[@]}"; do
  DIR="$SHOTS_OUT/$P"
  mkdir -p "$DIR"
  pebble kill >/dev/null 2>&1 || true

  if uses_fixtures "$P"; then
    # --- fixture flow (deterministic Helsinki data) ------------------------
    echo "==> Screenshots: $P (fixtures)"
    pebble install --emulator "$P" "$SHOT_PBW" >/dev/null 2>&1

    # Force a 24h clock for a uniform status bar. The format sticks (unlike the
    # time, which the phone re-syncs), so set it once here.
    pebble emu-time-format --emulator "$P" --format 24h >/dev/null 2>&1

    # Splash auto-dismisses after ~1.5s, so grab it first — the screenshot
    # round-trip lands inside that window. (No clock on the splash.)
    shot "$P" "$DIR/01-splash.png"

    # Splash -> Home (main menu). Shows "Pinned stops: 4 stops".
    sleep 4
    timed_shot "$P" "$DIR/02-home.png"

    # Home "Nearby stops" (top row) -> fixture stops (bus, metro, bus).
    press "$P" select; sleep "$LOAD_WAIT"
    timed_shot "$P" "$DIR/03-nearby-stops.png"

    # Back to Home, down to "Pinned stops" -> fixture pins (2 bus, 1 tram, 1
    # metro), tram first.
    press "$P" back; sleep 1
    press "$P" down; sleep 1
    press "$P" select; sleep "$LOAD_WAIT"
    timed_shot "$P" "$DIR/04-pins.png"

    # Open the tram pin (first row) -> fixture departures (tram; first two lines
    # on the minute display: "now" and "3 min").
    press "$P" select; sleep "$LOAD_WAIT"
    timed_shot "$P" "$DIR/05-departures.png"
  else
    # --- live-data fallback (heap-constrained platforms, e.g. aplite) ------
    # The fixture build overflows this platform's heap, so use the shippable
    # bundle with live debug-location data (the original approach). The data is
    # whatever Digitransit returns for the debug location, so these shots are not
    # the controlled set — they are the regular live screens.
    echo "==> Screenshots: $P (live fallback — fixture build does not fit its heap)"
    pebble install --emulator "$P" "$PBW" >/dev/null 2>&1
    pebble emu-time-format --emulator "$P" --format 24h >/dev/null 2>&1

    shot "$P" "$DIR/01-splash.png"
    sleep 4
    shot "$P" "$DIR/02-home.png"

    # Nearby stops -> first stop's departures.
    press "$P" select; sleep "$LOAD_WAIT"
    shot "$P" "$DIR/03-nearby-stops.png"
    press "$P" select; sleep "$LOAD_WAIT"
    shot "$P" "$DIR/04-departures.png"

    # Back to Home, down to Pinned stops.
    press "$P" back; sleep 1
    press "$P" back; sleep 1
    press "$P" down; sleep 1
    press "$P" select; sleep 2
    shot "$P" "$DIR/05-pinned-stops.png"
  fi

  pebble kill >/dev/null 2>&1 || true
  echo
done

finalize_release

echo "==> Done."
echo "    Bundle:      $PBW"
echo "    Changelog:   CHANGELOG.md (v$VERSION)"
echo "    Screenshots: $SHOTS_OUT/<platform>/"
if [[ $TAG_RELEASE -eq 0 ]]; then
  echo "    To mark this release in git (or re-run with --tag):"
  echo "      git add package.json CHANGELOG.md && git commit -m \"Release v$VERSION\" && git tag v$VERSION"
fi
if [[ $FAILS -gt 0 ]]; then
  echo "    NOTE: $FAILS screen(s) failed to capture — re-run, optionally with a"
  echo "          larger TREBBLE_LOAD_WAIT, to fill the gaps."
fi
