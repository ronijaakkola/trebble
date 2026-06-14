// Deterministic Helsinki fixture data for app-store / README screenshots.
//
// These stand in for the live Digitransit API so every release produces the same
// four screenshots regardless of the time of day, the network, or which stops
// happen to be running. They are served only when the watch signals screenshot
// mode (see SCREENSHOT_MODE in the release-build skill and app.js); production
// builds never reach this module, so the data here is purely cosmetic.
//
// The data is real Helsinki (HSL) territory: HSL gtfs-style ids (so the watch
// picks the right city header and mode colors — HSL buses cobalt blue, HSL trams
// green, the metro red), real stop names, line numbers and headsigns. Departure
// countdowns are baked in (lineMins/lineTime) rather than computed from the
// clock, so the departures screen is identical every run; pin the status-bar
// clock with `pebble emu-set-time` to match (the fixtures assume ~10:09).

// 1) Nearby stops — in the required order: bus, metro, bus. Distances are fixed so
//    the list never reorders.
var NEARBY_STOPS = [
  { stopMessage: 1, stopCode: "HSL:1040601", stopName: "Hakaniemi",      stopDist: 55,  stopMode: "BUS" },
  { stopMessage: 1, stopCode: "HSL:1040602", stopName: "Hakaniemi",      stopDist: 95,  stopMode: "SUBWAY" },
  { stopMessage: 1, stopCode: "HSL:1040603", stopName: "Hakaniementori", stopDist: 140, stopMode: "BUS" },
];

// 2) Pinned stops — four pins, one of each type: 2 buses, 1 tram, 1 metro. Ordered
//    nearest-first (the tram is first so the screenshot flow can open its
//    departures with a single select). The watch seeds the same four codes (see
//    pins_seed_fixtures) so the home-menu count matches this list.
var PINNED_STOPS = [
  { pinMessage: 1, pinCode: "HSL:1130439", pinName: "Ylioppilastalo",   pinDist: 60,  pinMode: "TRAM" },
  { pinMessage: 1, pinCode: "HSL:1040601", pinName: "Hakaniemi",        pinDist: 110, pinMode: "BUS" },
  { pinMessage: 1, pinCode: "HSL:1040703", pinName: "Kamppi",           pinDist: 350, pinMode: "SUBWAY" },
  { pinMessage: 1, pinCode: "HSL:1020201", pinName: "Rautatientori",    pinDist: 540, pinMode: "BUS" },
];

// 3) City shown in the menu header.
var CITY = "Helsinki";

// 4) City bike stations — four real Helsinki (smoove) stations with short names that
//    do not truncate, ordered nearest-first. Availability is fixed to exercise the
//    count colours: 7 green (>3), 0 grey, 3 red (1-3), 10 green. Names are already
//    code-stripped (the live path strips the leading station number). Served for a
//    bikeMessage in screenshot mode.
var BIKE_STATIONS = [
  { bikeMessage: 1, bikeCode: "smoove:071", bikeName: "Ruoholahti", bikeDist: 90,  bikeBikes: 7 },
  { bikeMessage: 1, bikeCode: "smoove:014", bikeName: "Hakaniemi",  bikeDist: 170, bikeBikes: 0 },
  { bikeMessage: 1, bikeCode: "smoove:003", bikeName: "Ooppera",    bikeDist: 260, bikeBikes: 3 },
  { bikeMessage: 1, bikeCode: "smoove:002", bikeName: "Kauppatori", bikeDist: 420, bikeBikes: 10 },
];

// 4) Departures, keyed by stop code. The screenshot flow opens the tram pin
//    (HSL:1130439), whose first two lines use the minute display: one "now" and
//    one "3" (both realtime, so they render green). Other codes fall back to a
//    bus/metro set so opening any stop still shows sensible data.
//    Each entry carries the stop-level zone + public code (shown on the
//    departures header / emery badge) and its ordered departures.
var DEPARTURES = {
  // Tram stop — the headline departures screenshot.
  "HSL:1130439": {
    stopZone: "A",
    stopShortCode: "H0301",
    lines: [
      { lineMessage: 1, lineCode: "4",  lineTime: "10:09", lineDir: "Munkkiniemi",       lineMode: "TRAM", lineRealtime: 1, lineMins: 0 },
      { lineMessage: 1, lineCode: "10", lineTime: "10:12", lineDir: "Kirurgi",           lineMode: "TRAM", lineRealtime: 1, lineMins: 3 },
      { lineMessage: 1, lineCode: "7",  lineTime: "10:15", lineDir: "Pasila",            lineMode: "TRAM", lineRealtime: 1, lineMins: 6 },
      { lineMessage: 1, lineCode: "2",  lineTime: "10:20", lineDir: "Olympiaterminaali", lineMode: "TRAM", lineRealtime: 0, lineMins: 11 },
      { lineMessage: 1, lineCode: "4",  lineTime: "10:23", lineDir: "Katajanokka",       lineMode: "TRAM", lineRealtime: 0, lineMins: 14 },
    ],
  },
  // Metro stops (Hakaniemi / Kamppi).
  "HSL:1040602": {
    stopZone: "A",
    stopShortCode: "M0001",
    lines: [
      { lineMessage: 1, lineCode: "M2", lineTime: "10:09", lineDir: "Tapiola",   lineMode: "SUBWAY", lineRealtime: 1, lineMins: 0 },
      { lineMessage: 1, lineCode: "M1", lineTime: "10:13", lineDir: "Vuosaari",  lineMode: "SUBWAY", lineRealtime: 1, lineMins: 4 },
      { lineMessage: 1, lineCode: "M2", lineTime: "10:17", lineDir: "Mellunmaki", lineMode: "SUBWAY", lineRealtime: 0, lineMins: 8 },
    ],
  },
};

// Bus departures used for any other stop (e.g. the first nearby stop).
var DEPARTURES_DEFAULT = {
  stopZone: "A",
  stopShortCode: "H1234",
  lines: [
    { lineMessage: 1, lineCode: "16", lineTime: "10:09", lineDir: "Kamppi",       lineMode: "BUS", lineRealtime: 1, lineMins: 0 },
    { lineMessage: 1, lineCode: "23", lineTime: "10:12", lineDir: "Eira",         lineMode: "BUS", lineRealtime: 1, lineMins: 3 },
    { lineMessage: 1, lineCode: "55", lineTime: "10:16", lineDir: "Koskela",      lineMode: "BUS", lineRealtime: 0, lineMins: 7 },
    { lineMessage: 1, lineCode: "65", lineTime: "10:21", lineDir: "Verakoy",      lineMode: "BUS", lineRealtime: 0, lineMins: 12 },
  ],
};

function departuresFor(code) {
  return DEPARTURES[code] || DEPARTURES_DEFAULT;
}

module.exports = {
  NEARBY_STOPS: NEARBY_STOPS,
  PINNED_STOPS: PINNED_STOPS,
  CITY: CITY,
  BIKE_STATIONS: BIKE_STATIONS,
  departuresFor: departuresFor,
};
