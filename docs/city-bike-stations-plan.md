# City bike stations — implementation plan

Status: **Implemented** (v1) — verified on basalt, chalk (round) and diorite emulators;
builds for all platforms. See "Implementation status (as built)" below for what shipped
and where it deviated from the original plan.

## Implementation status (as built)

Shipped in this branch (`city-bike-stations-plan`):

- **Data path:** `createBikeStationsQuery` (`src/pkjs/queries.js`) using
  `nearest(... filterByPlaceTypes:[VEHICLE_RENT])`; `getBikeStationsFromLocation` +
  dispatcher branch (`src/pkjs/app.js`). Sends `bikeMessage/bikeCode/bikeName/bikeDist/
  bikeBikes` per station; reuses `messageEnd/noInternet/noGps`. Only the **bikes count**
  is sent (no docks), per the design.
- **Watch UI:** new `src/c/bikes_window.{c,h}` (list = name + distance left, big bikes
  count right, no per-row badge/icon); third **"City bikes"** home-menu row
  (`src/c/home_window.c`); window registered in `src/c/main.c`. Empty state
  **"No bike stations nearby"**; errors route to the existing error screen.
- **Icon:** `resources/icons/25x-bike.pdc` wired as `IMAGE_BIKE` in `package.json`;
  drawn on the **home-menu row only** (not list rows). Tinted **`GColorChromeYellow`**
  (same for every city) on color platforms; B&W draws it in the normal inverted style.
- **New message keys** added to `package.json` (required a `pebble clean`, per memory).

Deviations / decisions made during implementation:

- **aplite is excluded from the feature.** The original Pebble/Steel (~24KB RAM) is
  already at its memory ceiling — the *unmodified* app shows `gpath` allocation
  failures and an occasional fault on the aplite emulator. The whole window is compiled
  out there (`PBL_PLATFORM_APLITE` guard in `bikes_window.c`), the home row is omitted,
  and the bike icon isn't loaded; aplite measured **no worse than the original**
  (≈8–9 gpath warnings, 1 fault, both before and after). Every other platform ships the
  full feature.
- **ChromeYellow is applied to the home-row icon, not the list count.** Yellow text on
  the white list row fails contrast; the icon carries the brand color instead.
- **The "City bikes" row is hidden in cities with no bikes.** Live data showed ~half
  the supported cities have no bike system, so the row is hidden there. Detection is
  dynamic and folded into the existing startup city-lookup query: it checks for any
  rental station within 5 km (the same radius used for city detection — measured to
  reach a station from essentially any in-city/suburb location; only true exurbs miss,
  and those have no bikes anyway). The phone sends a `cityHasBikes` flag; the watch
  hides the last home row when false (persisted so the first paint is right; defaults
  to shown until we positively learn otherwise). Caveat: if an operator pulls its
  stations off-season, the row hides until the season restarts — acceptable, since
  there'd be nothing to show. aplite is unaffected (the whole feature is gated off there).
- **Pinned bikes (§3.4) remain future work** (Phase 4) — not in v1.

The rest of this document is the original plan; sections it superseded are noted inline.

---

Original status: research + plan.

This document plans adding **city bike (bike-share) station** support to Vuoro
alongside the existing nearby-stops / departures feature. It covers the data
source, the exact GraphQL query and fields, the end-to-end data flow (JS → AppMessage
→ C model → UI), the UI/UX recommendation for the small Pebble screen, where the
city-bike **icon** gets wired in later, edge cases, and a phased breakdown.

The app today: a Pebble watchapp (C, Pebble SDK 3) with a PebbleKit JS phone
component (`src/pkjs/`). It already talks to the **Digitransit routing v2 GTFS
GraphQL API** for stops and departures, so city bikes can use the *same* endpoint
and the *same* request/transfer patterns — no new provider, no new auth.

---

## 1. Data source & available fields

### 1.1 Endpoint (unchanged)

The JS already targets the v2 routing endpoint (`src/pkjs/app.js:4`):

```
https://api.digitransit.fi/routing/v2/finland/gtfs/v1
```

Auth header is `digitransit-subscription-key: <API_KEY>` (`src/pkjs/app.js:122`),
set from `src/pkjs/secrets.js`. **City bikes need no new endpoint or key** — this
OTP2 GTFS GraphQL service exposes vehicle-rental data for the Finnish region,
fed from each operator's GBFS feed (HSL/Helsinki & Espoo, Föli/Turku, etc.).

### 1.2 The query — use `nearest(... filterByPlaceTypes: [VEHICLE_RENT])`

Digitransit deprecated the old `bikeRentalStation(s)` types. The current types are
`vehicleRentalStation` / `vehicleRentalStations` and the floating `rentalVehicle`.
There is **no `vehicleRentalStationsByRadius`** equivalent to `stopsByRadius`, so
the closest analogue to the existing nearby-stops query is the generic **`nearest`**
query with a place-type filter. It returns Relay-style `edges`, each carrying a
**`distance`** (meters, walking) and a `place` — exactly mirroring how
`stopsByRadius` returns `node.distance` today (`src/pkjs/queries.js:1-17`).

`place` is a union, so a `... on VehicleRentalStation` inline fragment selects the
station fields. Recommended query (new `createBikeStationsQuery(lat, lon, radius, limit)`
in `src/pkjs/queries.js`):

```graphql
{
  nearest(
    lat: ${latitude},
    lon: ${longitude},
    maxDistance: ${radius},
    maxResults: ${limit},
    filterByPlaceTypes: [VEHICLE_RENT]
  ) {
    edges {
      node {
        distance
        place {
          __typename
          ... on VehicleRentalStation {
            stationId
            name
            lat
            lon
            capacity
            operative
            realtime
            allowDropoff
            availableVehicles { total }
            availableSpaces  { total }
            rentalNetwork { networkId }
          }
        }
      }
    }
  }
}
```

Notes / gotchas confirmed against the OTP2 GTFS schema:

- **`filterByPlaceTypes: [VEHICLE_RENT]`** also matches floating `RentalVehicle`s
  (free-floating scooters/bikes). Guard with `__typename === "VehicleRentalStation"`
  in JS and drop everything else.
- **`availableVehicles` / `availableSpaces` are `RentalVehicleEntityCounts`, not
  ints.** Use the `total` subfield for a single number (`availableVehicles { total }`).
  `byType { count vehicleType { formFactor propulsionType } }` is available if we
  ever want to split e-bikes vs. regular — out of scope for v1.
- **`realtime: Boolean`** — if `false`, OTP returns `capacity / 2` as a placeholder
  for availability rather than live numbers. Treat non-realtime availability as
  "unknown" (see edge cases).
- **`operative: Boolean`** — station on and in service.
- **`stationId`** is `network:id` (e.g. `smoove:047`), analogous to a stop `gtfsId`.

### 1.3 Field reference

| Field | GraphQL type | Use in app |
|---|---|---|
| `stationId` | `String` | Station identity / key (e.g. `smoove:047`). |
| `name` | `String!` | List + detail title. |
| `lat`, `lon` | `Float` | Distance fallback if ever needed; not sent to watch. |
| `distance` (edge) | `Int` | Meters from user — drives sort + "%d m". |
| `availableVehicles { total }` | `Int!` | **Bikes available** (primary number). |
| `availableSpaces { total }` | `Int!` | Docks/spaces free — **not shown in v1** (§3.0); detail-screen only. |
| `capacity` | `Int` | Total racks (context; optional detail). |
| `operative` | `Boolean` | Out-of-service handling. |
| `realtime` | `Boolean` | Whether the two totals are live or estimated. |
| `allowDropoff` | `Boolean` | Optional: hint that returns are blocked. |
| `rentalNetwork { networkId }` | `String` | Operator/network (e.g. `smoove`, `vantaa`). Optional theming. |

### 1.4 Scoping & realtime

- **Scoping to location:** identical model to nearby stops — read the phone GPS
  (or the emulator debug location), pass `lat/lon` + a radius. Suggested radius
  **`bikeSearchDiameter = 500`** (m), matching `stopSearchDiameter` (`src/pkjs/app.js:70`),
  capped to ~10 results (`bikesLimit = 10`) to match the watch's fixed array and
  keep the AppMessage transfer short.
- **Sort:** `nearest` already returns results in ascending distance, so no client
  sort is needed (unlike pinned stops which sorts manually).
- **Refresh:** availability is volatile. Two realistic options:
  - **v1 (recommended): refresh-on-open only.** Re-fetch every time the bikes
    window appears. Simple, matches how nearby stops behave.
  - **v2 (optional): minute-tick refresh** like the departures window
    (`tick_timer_service_subscribe(MINUTE_UNIT, …)`, `src/c/lines_window.c:949`).
    Availability changes by the minute, so this is a reasonable later add. A 1–2 min
    silent refresh is plenty; sub-minute would burn battery/data for little gain.

---

## 2. End-to-end data flow

The feature reuses the established "watch asks → JS fetches → JS streams rows →
watch fills incrementally until `messageEnd`" pattern. Concretely:

```
Home menu "City bikes" row tapped
  └─ bikes_window pushed; bikes_window_load() sends { bikeMessage: 1 }   [C → JS]
        └─ app.js "appmessage" handler sees bikeMessage
              └─ get GPS (or debug loc in emulator)
                    └─ getBikeStationsFromLocation(pos):
                         createBikeStationsQuery(...) → POST GraphQL        [JS → Digitransit]
                         parse edges, filter to VehicleRentalStation
                         map each → per-station dict
                         sendList([...])                                   [JS → C, one msg/station]
  └─ bikes_message_inbox() accumulates rows into bikes[]                   [C]
        └─ on messageEnd: bikeAmount set, menu reloaded, loading cleared
```

### 2.1 JS side (`src/pkjs/`)

**`queries.js`** — add `createBikeStationsQuery(lat, lon, radius, limit)` (the query
in §1.2) and export it alongside the others (`src/pkjs/queries.js:89-94`).

**`app.js`** — add, mirroring `getStopsFromLocation` (`src/pkjs/app.js:138-193`):

- constants `bikeSearchDiameter = 500`, `bikesLimit = 10`.
- `getBikeStationsFromLocation(pos)`:
  - build query via `queries.createBikeStationsQuery`.
  - `createGraphQLRequest(url)` (reuses existing timeout / `noInternet` handling,
    `src/pkjs/app.js:118-136`).
  - on 200: parse, `response.data.nearest.edges`, then
    ```js
    .filter(e => e.node.place && e.node.place.__typename === "VehicleRentalStation")
    .filter(e => e.node.place.operative !== false)   // drop dead stations (see §5)
    .slice(0, bikesLimit)
    .map(e => {
      var p = e.node.place;
      return {
        bikeMessage: 1,
        bikeCode: p.stationId,
        bikeName: p.name,
        bikeDist: e.node.distance,
        bikeBikes: p.realtime ? p.availableVehicles.total : -1,
        // bikeDocks omitted in v1 — the list shows only bikes (§3.0/§3.2).
        // Add back when a detail screen needs it:
        // bikeDocks: p.realtime ? p.availableSpaces.total : -1,
      };
    });
    ```
    (`-1` = "availability unknown / not realtime"; the C side renders it as `—`.)
  - `sendList(stations)` — the existing `sendList`/`sendNextItem` already handle the
    empty-list `messageEnd` terminator correctly (`src/pkjs/app.js:105-116`), so an
    empty result cleanly drives the watch's empty state.
  - on no data / non-200: `Pebble.sendAppMessage({ bikeNoFound: 1 })`.
- Wire a new branch into the `"appmessage"` dispatcher (`src/pkjs/app.js:529-571`),
  parallel to the `stopMessage` branch — GPS lookup with emulator debug-location
  shortcut, and `{ noGps: 1 }` on GPS failure (reuse the existing key).

### 2.2 AppMessage keys / payload

Add these `messageKeys` to `package.json` under `pebble.messageKeys`
(`package.json:9-40`). Keep names consistent with the `stop*`/`pin*` convention:

| New key | Direction | Meaning |
|---|---|---|
| `bikeMessage` | C→JS request **and** JS→C per-station marker | "fetch bikes" / "a station row follows" |
| `bikeCode` | JS→C | `stationId` |
| `bikeName` | JS→C | station name |
| `bikeDist` | JS→C | distance in meters (uint) |
| `bikeBikes` | JS→C | available bikes (int; `-1` = unknown) — the one number the list shows (§3.0) |
| `bikeNoFound` | JS→C | no stations / fetch returned nothing usable |

`bikeDocks` (available spaces) is **not** in v1 — the designed list shows bikes only
(§3.0/§3.2). Add it (and its `messageKey`) only if/when a detail screen needs docks.
That keeps the v1 key count at six and the payload minimal.

Reused existing keys: **`messageEnd`** (end-of-list terminator), **`noInternet`**,
**`noGps`**. No new error keys needed for those paths.

> ⚠️ **Build note (project memory):** adding any new `messageKey` to `package.json`
> requires a **`pebble clean`** before the next build, or the C compile fails with
> `MESSAGE_KEY_bikeBikes undeclared` (and friends) because the generated
> `message_keys.auto.h` is stale. Do the clean rebuild immediately after editing
> `package.json`.

**Payload-size sanity:** one station per AppMessage (the existing per-item streaming
model), so the inbox only ever holds a single small dict: a ~24-byte id, a ≤30-byte
name, and two small ints (distance, bikes). Comfortably within the AppMessage inbox
size; identical in shape to the `stop*` rows already sent. No batching or compaction
needed.

### 2.3 C side — model

New `src/c/bikes_window.c` + `.h`, modeled directly on `main_window.*`
(nearby stops). Data model mirroring `struct StopInfo` (`src/c/main_window.h`):

```c
#define NUM_BIKE_STATIONS 10

struct BikeStationInfo {
  char code[24];   // stationId, e.g. "smoove:047" (slightly longer than stop ids)
  char name[30];   // station name
  int  dist;       // meters
  int  bikes;      // available bikes  (-1 = unknown / not realtime) — the value the list shows
  // int docks;    // available spaces — deferred with bikeDocks (§2.2); add for a detail screen
};

static struct BikeStationInfo bike_stations[NUM_BIKE_STATIONS];
static int bikeAmount = 0;
// + the same transfer-state statics main_window uses:
// bike_transfer_started, bike_index, bikes_loaded, savedSelectedRow
```

Inbox handler `bikes_message_inbox()` copies `main_message_inbox()`
(`src/c/main_window.c:104-167`): on `bikeMessage` start/continue a transfer, route
each tuple through a `process_bike_tuple()` into `bike_stations[bike_index]`,
increment per station; on `messageEnd` set `bikeAmount`, reload the menu, clear
loading, vibrate, and show the empty state when `bikeAmount == 0`; on `noInternet`
/ `noGps` / `bikeNoFound` route to the error window exactly as stops do.

`bikes_window_load()` sends the request, mirroring `main_window_load()`
(`src/c/main_window.c:540-548`):

```c
dict_write_uint8(iter, MESSAGE_KEY_bikeMessage, 1);
```

### 2.4 C side — window registration

- `main.c`: create/register the bikes window during `init()` next to the other
  windows, and ensure its inbox handler participates in the same single AppMessage
  channel (each window registers `app_message_register_inbox_received` on load, per
  the existing per-window-handler pattern — there is **no** central dispatcher, see
  `src/c/main.c`).
- `home_window.c`: add the third menu row (see §3).

---

## 3. UI / UX recommendation

### 3.0 Design reference (Pencil mockups) — source of truth

Two screens were designed in Pencil (`pebble.pen`) and are the authoritative layout
for the **rectangular 144×168 platforms** (basalt, aplite, diorite); round (chalk) and
Emery adapt from them (notes below). The mockups use Raleway as a stand-in — the device
uses the existing system **Gothic** fonts.

**Home menu** (frame `Home`): status bar (time) → dotted divider → centered title →
three rows. The new **third row "City bikes"** sits below Nearby/Pinned, drawn with a
**bike icon** (mocked with the Lucide `bike` glyph; ships as the PDC bike icon, §4) at
25 px, label "City bikes" (bold ~15). It matches the existing row layout exactly; the
selected row uses the grey highlight. (The mock's title reads "Trebble" — that header
already shows the detected city / app name at runtime, unchanged by this feature.)

**City Bikes · Nearby Stations** (frame `City Bikes · Nearby Stations`): status bar →
dotted divider → centered header **"City bikes"** (bold ~12) → station rows. Each row
(height ~44, padding ~[5,6], horizontal, items centered):
- **Left — name + distance**, stacked, filling the row width: station **name** (bold
  ~14, black) over **distance** (`140 m`, ~12, grey `#555`).
- **Right — a count block**, vertically centered: a large **number** (bold ~20, black)
  over the unit **"bikes"** (~9, grey `#555`).

Key decisions the design locks in (and that revise earlier drafts of this plan):
1. **The list shows only the available-bikes count — no docks/"free" in the list, and
   no per-row badge or mode icon.** The mode is conveyed by the screen header, so the
   row needs no `B`/icon at all. (This supersedes the earlier "N bikes · M free" row and
   the yellow-`B`-badge idea for this list — see §3.2.)
2. **Raw count, prominently** (big number), e.g. `12` / `6` / `2`. Confirms the
   no-fraction decision (§3.2).
3. **Zero is shown as `0 bikes`**, station still listed (the mock's "Rautatieasema" row)
   — zero availability is a real, displayed state, not a filter.

Suggested device-font mapping: name → `FONT_KEY_GOTHIC_18_BOLD` (as stops use), distance
→ `FONT_KEY_GOTHIC_14` grey, count number → a large bold face (e.g.
`FONT_KEY_GOTHIC_24_BOLD`), unit → `FONT_KEY_GOTHIC_14`/smallest available, grey.

Platform adaptation (not mocked): **round/chalk** — center the row content; stack the
count under the name+distance, or keep name centered with the count on the line below
(mirror the stops round layout, swapping the badge+distance line for the count). **Emery**
— same as rect with the larger cell height; the count block can sit right-aligned.

### 3.1 Where it lives — a third home-menu mode

The home menu (`src/c/home_window.c`) currently has two rows — **Nearby stops**
and **Pinned stops** (`NUM_HOME_ITEMS 2`, rows `HOME_ROW_NEARBY` / `HOME_ROW_PINNED`).
Add a **third row "City bikes"**:

- `#define NUM_HOME_ITEMS 3`, `#define HOME_ROW_BIKES 2`.
- append `{ "City bikes", "" }` to `home_items[]`.
- in `home_menu_select_callback()` add a `case HOME_ROW_BIKES:` that pushes the
  bikes window (`window_stack_push(bikes_window_get_window())`), next to the existing
  nearby/pinned routes (`src/c/home_window.c:263-275`).
- draw the row with the new bike PDC icon (see §4), the same way the menu draws
  `nearbyIcon` / `pinnedIcon`.

**Why a separate mode (not mixed into nearby stops):** bike stations carry totally
different data (bikes/docks vs. departures), have no "lines" detail, and are
seasonal/region-limited. Mixing them into the stops list would complicate the
mode-icon/badge logic and the select→departures flow. A dedicated mode keeps each
list simple and is the smallest, cleanest change. This mirrors how Pinned stops is
already its own mode.

### 3.2 The bikes list screen

Built from the `City Bikes · Nearby Stations` mockup (§3.0). It reuses the
nearby-stops list scaffolding (`src/c/main_window.c` — MenuLayer, centered header,
marquee for long names, loading/empty overlay) but **not** the stop row's badge: a
bike row is **name + distance on the left, a bikes-count block on the right**, with no
mode icon or letter badge.

Row layout (rect — `main_window.c` row-draw is the starting point):

```
Keskustori          12
140 m            bikes
```

- **Left:** station **name** (`FONT_KEY_GOTHIC_18_BOLD`, scrolls via the existing
  marquee when focused + overflowing) over **distance** `%d m`
  (`FONT_KEY_GOTHIC_14`, grey), matching how stops draw name + distance
  (`src/c/main_window.c:277`).
- **Right:** a vertically-centered **count block** — the available-bikes **number**
  in a large bold face (e.g. `FONT_KEY_GOTHIC_24_BOLD`) over the unit **"bikes"**
  (small, grey). This is the focal element of the row.
- **No per-row badge or icon, on any platform.** The screen header ("City bikes")
  establishes the mode, so the `B`/yellow-badge and bike-icon-per-row ideas from
  earlier drafts are dropped here. (The bike icon survives only on the Home menu row,
  §3.1/§4.) This also makes the earlier "`B` clashes with bus" concern moot for this
  list — there is no letter to clash.
- **Raw count, no `bikes/capacity` fraction.** The mock shows `12` / `6` / `2`, not
  `12/100`. Rationale unchanged: "can I grab a bike now?" is answered instantly by the
  raw number; a denominator forces mental math, eats width (Tampere ≈100 racks), and
  isn't even dependable (`availableVehicles.total + availableSpaces.total` need not
  equal `capacity`). Keep `capacity`/docks for an optional future detail screen only.
- **Zero shows as `0`** with the station still listed (mock's "Rautatieasema") — a real
  state, not a filter.
- **Unknown availability** (`bikes == -1`, non-realtime, §2.1): render `—` in place of
  the number (unit still "bikes"), or a small "n/a".
- Optional color cue on color watches: number green when `> 0`, grey when `0`. Subtle;
  skip on b&w.

> Docks/"free" are intentionally **not** in this list (design decision §3.0). If a
> detail screen is added later (§3.3), that's where docks/capacity belong.

### 3.3 Detail screen — optional, keep minimal

Unlike stops, bike stations have **no departures**, so there is no rich detail to
page — and the Pencil designs (§3.0) include **no detail screen**, only the list.

- **v1 (matches the design): no detail window.** The bikes count lives in the list
  row (§3.2); the row is non-actionable, or select just re-triggers a refresh. Least
  code, and shows the one number that matters at a glance.
- **Optional later: a tiny detail window** — the natural home for the fields the list
  omits: **docks/spaces free**, `capacity`, network, and an out-of-service note. Would
  reuse the lines-window header pattern (`src/c/lines_window.c:402`) without the
  departures list. Not designed yet; would need a mockup first.

### 3.4 Pinning bikes — into the **same** pinned menu (recommended)

Goal: long-press a nearby bike station to pin it, and have it show up in the existing
**Pinned** menu alongside pinned stops. This is feasible with a small change because
the pins storage is already kind-agnostic.

> Not yet designed: the Pencil mockups (§3.0) cover Home + the nearby-bikes list, but
> **not** a mixed pinned menu. The approach below is a proposal pending a mockup.

**Storage — `type` becomes an internal *kind* marker (no migration).**
`struct PinnedStop { char code[20]; char name[30]; char type[2]; }` (`src/c/pins.c`)
already carries `type` (`'B'`/`'T'`/`'\0'`). Add a bike kind marker as a named constant:

```c
#define PIN_TYPE_BIKE 'K'   // internal kind only — never drawn (bike rows show a count, not a letter)
```

`type[0]` becomes a pure kind tag: `'B'` bus, `'T'` tram, `PIN_TYPE_BIKE` bike. The
mixed pinned list uses it to pick the resolver (`stops(ids:)` vs
`vehicleRentalStation(id:)`), the select action, and the section bucket (§ below). It is
never blitted — bike rows render the **count block** (name + distance + bikes count,
exactly like the nearby-bikes row §3.2), which is what visually distinguishes a bike row
from a stop row (the stop row keeps its mode badge). No letter badge, so no `B`-vs-bus
clash, and no need for a separate `kind` field (which would force a struct +
persistence-format change). `'K'` is just for code readability.

Existing persisted pins (all stops) keep working untouched, and `pins_toggle` /
`pins_is_pinned` / `pins_build_codes_csv` are already type-agnostic — the nearby-bikes
long-press just calls `pins_show_action_menu(stationId, name, PIN_TYPE_BIKE, …)`.

> Width check: `code[20]` fits stop gtfsIds and short `network:id` station ids, but
> confirm the longest real `stationId` fits. If not, bump to `char code[24]` — a
> one-time persisted-format change, so do it *before* shipping pinned bikes.

**JS resolve — one combined query for both kinds.** `handlePinnedStops` splits the
pinned CSV by kind and resolves both in a single GraphQL document: `stops(ids:[…])`
for stop ids (as today, `src/pkjs/queries.js:71`) **plus** aliased
`vehicleRentalStation(id:)` lookups for the bikes (there is no `vehicleRentalStations(ids:)`,
but MAX_PINS=10 keeps the alias count trivial):

```graphql
{
  stops(ids: ["HSL:1234", ...]) { gtfsId name lat lon vehicleMode }
  b0: vehicleRentalStation(id: "smoove:047") {
    stationId name lat lon operative realtime
    availableVehicles { total }
  }
  b1: vehicleRentalStation(id: "...") { ... }
}
```

Merge the two result sets, compute distance client-side with the existing
`distanceMeters` (`src/pkjs/app.js:383`) — bike stations carry `lat/lon`, so the same
nearest-first sort spans both kinds — and stream pin rows tagged by kind. Bike rows
set `pinMode` to the bike marker and include the bikes count (reuse `bikeBikes`; same
bikes-only display as the nearby list, §3.2). A `vehicleRentalStation(id:)` that returns
`null` (removed / off-season) is dropped, exactly like the existing null-stop handling
in `getPinnedStops`.

**Select action + rendering — branch on kind in `pins_window.c`.** A pinned **stop**
row behaves as today (select → departures / lines window). A pinned **bike** row
instead renders the **bikes-count block** (name + distance + count, §3.2; fresh because
the pinned menu re-fetches on open), and select is a no-op / refresh, matching the
list-only bikes view (§3.3). The pinned empty-state copy ("long-press a nearby stop…")
should be reworded to mention bikes too.

**Separate the two kinds with section subheadings.** Render the pinned menu as two
native MenuLayer **sections** — section 0 **"Stops"**, section 1 **"City bikes"** — each
with its own header (`get_num_sections` → 2, per-section `get_header_height` /
`draw_header`), omitting a header for any empty section. This is the visual divider
that disambiguates bikes from stops, and it is what makes the shared-`B`-on-B&W case
(§3.2) unambiguous. Trade-off: ordering becomes nearest-first *within* each section
rather than one global nearest-first list — acceptable, and arguably clearer. Bucket
each pinned entry into its section by `type == PIN_TYPE_BIKE`. (Only the pinned menu
needs sections; the nearby-bikes list is a single-kind mode.)

This keeps a single Pinned menu (the UX the feature wants) and avoids a second
persistence namespace. It depends on the nearby-bikes list (Phases 1–3) existing
first, so it lands as **Phase 4**.

---

## 4. Icon wiring (user supplies the icon later)

The app draws its in-list icons as **Pebble Draw Command (`.pdc`) vector files**,
loaded with `gdraw_command_image_create_with_resource(...)` and recolored at draw
time via the `pdc_set_colors()` helper (see `src/c/main_window.c` bus/tram icons and
`src/c/home_window.c` menu icons). The existing in-list icons are **25 px** PDCs
(`resources/icons/25px-bus.pdc`, `25px-stop.pdc`, `25px-tram.pdc`); SVG sources sit
next to them (`resources/icons/25px-bus.svg`) and are converted to `.pdc`.

### What to add when the icon arrives

1. **Drop the files** into `resources/icons/`:
   - `resources/icons/25px-bike.svg` (editable source, ~25×25 px, single-color path
     to match the monochrome-then-recolored bus/tram/stop icons).
   - `resources/icons/25px-bike.pdc` (the compiled Draw Command file the app actually
     loads). Generate it from the SVG the same way the other `.pdc` files were made
     (Pebble's `svg2pdc` / the `pebble` tooling). **PDC, not PNG**, so `pdc_set_colors()`
     recoloring works on color and b&w platforms alike.
   - *(Placeholder until then: copy `25px-stop.pdc` to `25px-bike.pdc` so the build
     links and the row renders a stand-in glyph. Clearly a placeholder — swap on
     delivery.)*
2. **Register the resource** in `package.json` → `pebble.resources.media`
   (`package.json:42-110`), following the existing `IMAGE_BUS` entry exactly:
   ```json
   {
     "file": "icons/25px-bike.pdc",
     "name": "IMAGE_BIKE",
     "targetPlatforms": null,
     "type": "raw"
   }
   ```
   `type: "raw"` + `null` platforms is what every other `.pdc` icon uses (PDCs are
   loaded as raw resources, unlike the `bitmap` PNGs). The C code then references it
   as `RESOURCE_ID_IMAGE_BIKE`.
3. **Where it's used: the "City bikes" Home-menu row only** (`home_window.c`, alongside
   `nearbyIcon`/`pinnedIcon` — confirmed by the Home mockup §3.0). Load it there with
   `gdraw_command_image_create_with_resource(RESOURCE_ID_IMAGE_BIKE)`, free it in the
   window's destroy, and recolor with `pdc_set_colors()` before
   `gdraw_command_image_draw()`. The **bikes list rows do not use an icon** (§3.2,
   §3.0) — so the icon is *not* needed in `bikes_window.c`. (A larger splash/menu
   variant remains optional, step 4.)
4. **(Optional) menu / splash icons.** If a "City bikes" menu glyph or a larger
   splash variant is wanted, add `50px-bike.pdc` etc. with their own media entries,
   mirroring `IMAGE_SPLASH_BUS` / the home menu icons. Not required for v1.

> This project uses `package.json`'s `pebble` block as its manifest (there is **no
> separate `appinfo.json`**), so all resource + messageKey edits happen there.
> Per-platform note: a single `.pdc` with `targetPlatforms: null` already serves all
> five targets (aplite, basalt, chalk, diorite, emery); no per-platform asset is
> needed. Adding a media resource does **not** require `pebble clean` (only new
> *messageKeys* do), but a clean rebuild is harmless.

---

## 5. Edge cases

| Case | Handling |
|---|---|
| **No nearby stations** | `nearest` returns empty `edges` → `sendList([])` → existing empty-list `messageEnd` path → bikes window shows an empty state ("No bike stations nearby"), reusing the stops empty-state mechanism (`src/c/main_window.c` loading/empty layer). |
| **Region without city bikes / off-season** | Helsinki city bikes run ~Apr–Oct; many cities have none. Same as "no nearby stations" — empty list, friendly empty message. Optionally tailor copy ("No city bikes here / out of season"). |
| **Station out of service** | `operative === false` filtered out in JS (§2.1). If we instead keep them, the C row should show an "out of service" note and suppress the numbers. Recommended: filter them out for v1. |
| **Stale / non-realtime availability** | `realtime === false` → OTP returns `capacity/2`, which is misleading. JS sends `bikeBikes = -1`; C renders `—` instead of a fake number. |
| **Zero availability** | `0 bikes` is a valid, real state — show it (mock's "Rautatieasema" row, §3.0). Distinct from `-1`/unknown. |
| **Fetch failure (offline/timeout)** | `createGraphQLRequest` already fires `noInternet` on `onerror`/`ontimeout` (`src/pkjs/app.js:127-134`); the bikes window routes `noInternet` to the error window like stops do. |
| **GPS failure** | JS sends `{ noGps: 1 }` (reused key); bikes window shows the no-GPS error, same as nearby stops. |
| **Floating vehicles in results** | `filterByPlaceTypes: [VEHICLE_RENT]` can include `RentalVehicle`s; the `__typename` filter drops them so only docking stations render. |
| **Long station names** | Reuse the marquee scroller already used for stop/line names (`src/c/marquee.*`). |
| **Name encoding** | Finnish names include ä/ö; the existing stop/line rendering already handles these, so no new work. |

---

## 6. Open questions / risks

1. **Does `nearest` + `VEHICLE_RENT` return data on the `finland` v2 feed?** Very
   likely (it aggregates the operator GBFS feeds), but worth a one-off GraphiQL check
   against the production endpoint for a Helsinki coordinate before building the C UI.
2. **Detail screen or list-only?** The design is list-only (§3.0/§3.3), showing the
   bikes count only. Open: whether docks/capacity ever warrant a tap-through detail
   screen (would need its own mockup).
3. **Refresh cadence** — open-only (v1) vs. minute-tick (v2). Decide based on how
   "live" the user expects the numbers to feel vs. battery/data.
4. **e-bikes split** — `availableVehicles.byType` can separate electric vs. regular.
   Out of scope for v1; flag if it matters to users.
5. **Pinning bike stations** — supported via the unified pinned menu (§3.4); reuses
   the `type` byte as a kind discriminator (`PIN_TYPE_BIKE = 'K'`, no persistence
   migration) and a combined stops + aliased-`vehicleRentalStation` resolve. Lands as
   Phase 4.
6. **Per-network theming** — `rentalNetwork.networkId` could drive a brand color via
   `region.c` (`src/c/region.c`), but city bikes are largely single-operator per
   region; low priority.
7. **Memory on aplite** — the bikes window adds one more set of menu/icon layers.
   Follow the existing build/destroy-on-appear/disappear discipline
   (`src/c/main_window.c` lifecycle) so only the visible window holds layers; a new
   fixed `bike_stations[10]` array is small.

---

## 7. Suggested phased breakdown

- **Phase 0 — plumbing.** Add the seven `messageKeys` to `package.json`; **`pebble
  clean`** rebuild (memory note). Add the `IMAGE_BIKE` media entry; drop in a clearly
  flagged placeholder `25px-bike.pdc` (copy of the stop icon) until the real icon
  arrives.
- **Phase 1 — JS data path.** `createBikeStationsQuery` in `queries.js`;
  `getBikeStationsFromLocation` + dispatcher branch in `app.js`. Verify end-to-end
  with `console.log` against the emulator debug location (Helsinki) before any C UI.
- **Phase 2 — C model + minimal window.** `bikes_window.c/.h` (model, inbox, load/
  request, loading + empty states) cloned from `main_window`; third home-menu row with
  the bike icon. Render the designed row (§3.0/§3.2): name + distance left, bikes-count
  block right; no per-row badge/icon.
- **Phase 3 — rendering polish.** Per-platform layouts (round/Emery adapt from the rect
  mock, §3.0), marquee on names, optional color cue on the count.
- **Phase 4 — pinned bikes + extras.** Pin bike stations into the unified Pinned menu
  (§3.4: `PIN_TYPE_BIKE = 'K'` kind marker, combined JS resolve, kind-branched
  select/rendering).
  Plus optional: minute-tick refresh, out-of-service detail, e-bike split.

---

## 8. Touchpoint checklist

| Area | File | Change |
|---|---|---|
| Query | `src/pkjs/queries.js` | add + export `createBikeStationsQuery` |
| Fetch/parse/send | `src/pkjs/app.js` | `getBikeStationsFromLocation`, dispatcher branch, constants |
| Message keys | `package.json` (`pebble.messageKeys`) | add 7 keys → **`pebble clean`** |
| Icon resource | `package.json` (`pebble.resources.media`) | add `IMAGE_BIKE` (raw pdc) |
| Icon assets | `resources/icons/25px-bike.{svg,pdc}` | user-supplied later (placeholder ok) |
| New window | `src/c/bikes_window.c` / `.h` | model + inbox + UI (clone of `main_window`) |
| Window reg. | `src/c/main.c` | create/register bikes window |
| Home menu | `src/c/home_window.c` | 3rd row "City bikes" + select route + icon |
| (opt) theming | `src/c/region.c` | per-network color (later) |
| (opt) pinned bikes | `src/c/pins.c`, `src/c/pins_window.c`, `src/pkjs/*` | `PIN_TYPE_BIKE = 'K'` kind marker in `type`; combined stops + `vehicleRentalStation` resolve; kind-branched select/render (Phase 4, §3.4) |

---

### Sources

- Digitransit — Bicycles, scooters & cars (vehicle rental): https://digitransit.fi/en/developers/apis/1-routing-api/bicycles-scooters-cars/
- Digitransit — Routing API GraphQL: https://digitransit.fi/en/developers/apis/1-routing-api/0-graphql/
- OTP2 GTFS GraphQL — `VehicleRentalStation`: https://docs.opentripplanner.org/api/dev-2.x/graphql-gtfs/types/VehicleRentalStation
- OTP2 GTFS GraphQL — `RentalVehicleEntityCounts`: https://docs.opentripplanner.org/api/dev-2.x/graphql-gtfs/types/RentalVehicleEntityCounts
- OTP2 GTFS GraphQL — `nearest` query: https://docs.opentripplanner.org/api/dev-2.x/graphql-gtfs/queries/nearest
