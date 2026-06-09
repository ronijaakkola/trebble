#!/usr/bin/env bash
#
# Prepare Trebble release artifacts:
#   1. Clean build of the multi-platform .pbw bundle.
#   2. A versioned copy of the bundle into release/builds/.
#   3. Screenshots of every screen on every target platform into
#      release/screenshots/<platform>/.
#
# Everything lands under release/, which is gitignored.
#
# Usage:
#   scripts/release.sh                 # build + screenshot all platforms
#   scripts/release.sh --no-shots      # build the bundle only, skip screenshots
#   scripts/release.sh basalt chalk    # build + screenshot only these platforms
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
PLATFORMS=()
for arg in "$@"; do
  case "$arg" in
    --no-shots) TAKE_SHOTS=0 ;;
    -*) echo "error: unknown flag $arg" >&2; exit 1 ;;
    *) PLATFORMS+=("$arg") ;;
  esac
done
if [[ ${#PLATFORMS[@]} -eq 0 ]]; then
  read -r -a PLATFORMS <<< "$ALL_PLATFORMS"
fi

# How long to wait (seconds) after a button press for a network-backed screen
# to load. The app fetches live departures from the Digitransit API, so this is
# sized for that round trip plus rendering. Bump it if screens come up blank.
LOAD_WAIT="${TREBBLE_LOAD_WAIT:-6}"

BUILD_OUT="release/builds"
SHOTS_OUT="release/screenshots"

echo "==> Trebble release v$VERSION"
echo "    platforms: ${PLATFORMS[*]}"
echo

# --- 1. clean build --------------------------------------------------------
echo "==> Building (clean)…"
pebble clean >/dev/null 2>&1 || true
pebble build 2>&1 | tail -3

mkdir -p "$BUILD_OUT"
PBW="$BUILD_OUT/trebble-$VERSION.pbw"
cp build/trebble.pbw "$PBW"
echo "==> Bundle: $PBW"
echo

if [[ $TAKE_SHOTS -eq 0 ]]; then
  echo "==> Skipping screenshots (--no-shots). Done."
  exit 0
fi

# --- 2. screenshots per platform ------------------------------------------
# Emulator interactions are inherently flaky (cold boots, transient timeouts),
# and the bundle is already built by this point, so don't let one bad step abort
# the whole run. Tolerate failures, warn, and keep going.
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

for P in "${PLATFORMS[@]}"; do
  echo "==> Screenshots: $P"
  DIR="$SHOTS_OUT/$P"
  mkdir -p "$DIR"

  # Fresh emulator so navigation always starts from the splash screen.
  pebble kill >/dev/null 2>&1 || true
  pebble install --emulator "$P" "$PBW" >/dev/null 2>&1

  # Splash auto-dismisses after ~1.5s, so grab it first — the screenshot
  # round-trip itself lands inside that window.
  shot "$P" "$DIR/01-splash.png"

  # Splash -> Home menu.
  sleep 4
  shot "$P" "$DIR/02-home.png"

  # Home "Nearby stops" (top row) -> live stops list.
  press "$P" select
  sleep "$LOAD_WAIT"
  shot "$P" "$DIR/03-nearby-stops.png"

  # First stop -> live departures list.
  press "$P" select
  sleep "$LOAD_WAIT"
  shot "$P" "$DIR/04-departures.png"

  # Back to Home, then down to "Pinned stops" -> pinned list.
  press "$P" back; sleep 1
  press "$P" back; sleep 1
  press "$P" down; sleep 1
  press "$P" select; sleep 2
  shot "$P" "$DIR/05-pinned-stops.png"

  pebble kill >/dev/null 2>&1 || true
  echo
done

echo "==> Done."
echo "    Bundle:      $PBW"
echo "    Screenshots: $SHOTS_OUT/<platform>/"
if [[ $FAILS -gt 0 ]]; then
  echo "    NOTE: $FAILS screen(s) failed to capture — re-run, optionally with a"
  echo "          larger TREBBLE_LOAD_WAIT, to fill the gaps."
fi
