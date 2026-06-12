# Subway / metro support

The app renders **subway / metro** as a transit mode alongside bus and tram. It is
generic: any region whose Digitransit feed exposes subway data lights up. In
Finland only Helsinki (HSL) has a metro today, but nothing is hardcoded to HSL.

## How a mode flows through the app

Digitransit reports a `vehicleMode` / `route.mode` string → the phone JS filters
and forwards it over AppMessage → C collapses it to a single-letter **type** that
drives the icon, badge letter, and color.

- **Data field:** `"SUBWAY"` (GTFS `route_type 1`). Digitransit normalizes metro to
  this string, so we key off the string, not the numeric type.
- **JS filter:** `isSupportedMode()` in `src/pkjs/app.js` allows
  `"BUS" | "TRAM" | "SUBWAY"`; everything else is dropped before it reaches the
  watch. The mode rides existing AppMessage keys (`stopMode` / `lineMode` /
  `pinMode`) — no new message keys, and the GraphQL queries already fetch the mode
  field, so `queries.js` is untouched.
- **Type letter:** `"SUBWAY" → 'M'`, mapped by the shared
  `region_mode_to_type_letter()` in `src/c/region.c` (one function for all three
  windows). The letter is stored in the 2-char `type` field on `StopInfo`,
  `PinnedStop`, and the departure rows.
- **Color:** `GColorRed` for every metro, in `region_mode_color()` (`region.c`).
  HSL's metro brand is orange (`#CA4000`), but red reads cleanly on the watch and
  is the project choice; since only HSL has a metro this only affects Helsinki. The
  `'M'` case is structured like the tram case so a region-specific color can be
  added later. The hardcoded `'B' ? blue : red` badge paths already yield red for
  `'M'`; `pins.c`'s action-menu background was updated to treat `'M'` as red too.
- **Icon vs letter:** platforms that draw mode icons (Emery, and the Chalk/Round
  header) show the **subway icon**; the others (Basalt, Aplite, Diorite lists) show
  the **M** letter badge.

## Icon resource

`resources/icons/25px-subway.pdc` (with `25px-subway.svg` source), registered in
`package.json` as `IMAGE_SUBWAY` (`type: raw`, `targetPlatforms: null`).

- **Format:** a 25×25 px Pebble Draw Command (`.pdc`) vector, white fill + ~2 px
  black stroke, matching `25px-bus`/`25px-tram`. Fill/stroke are overridden at draw
  time via `pdc_set_colors()`, so only the two-tone structure matters. Convert
  SVG→PDC with Pebble's `svg2pdc.py`; the `.pdc` is committed directly (no build
  step). A single file covers every platform — these vectors render on both B&W and
  color watches.
- The icon is loaded as `subwayIcon` next to `busIcon`/`tramIcon` in `main_window`,
  `pins_window`, and `lines_window` (created/destroyed with each window) and
  selected via the `type == 'M' ? subwayIcon : …` ternaries.

## Testing notes

- **Debug location:** `debugCity = "helsinki"` → `[60.18048, 24.95017]`, the
  **Hakaniemi metro entrance**, centred on the metro stop so it ranks first in the
  nearby list, with Hakaniemi's trams and buses just outside — all three modes show
  at once (good for screenshots).
  - **Avoid the central railway station (Rautatientori):** Helsinki Central has
    ~54 GTFS `RAIL` platform stops that we filter out, but they still consume the
    `stopsByRadius(first: 20)` budget in `createStopsQuery`, so the metro (~116 m
    away) is dropped before the filter runs. Dense bus terminals (Kamppi) bury it
    too. Hakaniemi has no rail station, so the metro survives. If the metro ever
    needs to appear near rail hubs in production, raise `first:`.
- **Regions without a metro** (e.g. Tampere) are inert — no `SUBWAY` stops are
  returned, so bus/tram behavior is unchanged.
- **Verified in the emulator** against live HSL data at Hakaniemi: red **M** badge
  in the nearby list (white M on black on B&W watches), red header bar with the
  subway icon when opened, and real **M1 / M2** line badges ("Mellunmäki via
  Itäkeskus"). Bus (blue **B**) and tram (green **T**) badges unaffected. Worth a
  manual pass on pinning a metro stop and the metro action-menu (red) bar.
- **⚠️ Aplite RAM budget:** aplite has ~24 KB RAM and loads its *code* into that
  RAM, leaving only ~3.5 KB free heap. An early version of this feature added ~95
  bytes of code and aplite started **crashing on launch** (`App fault!` with a PC
  in firmware = a system allocation failing under low heap) on every city.
  Consolidating the three duplicate `mode_to_type_letter` copies into the single
  shared `region_mode_to_type_letter()` reclaimed ~130 bytes and restored the
  margin. Watch the `Free RAM available (heap)` line in aplite's build output —
  under ~3.5 KB is fragile, and no other platform (43 KB free) will warn you.
