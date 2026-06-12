const secrets = require("./secrets");
const queries = require("./queries");

var url = "https://api.digitransit.fi/routing/v2/finland/gtfs/v1";

var geolocationOptions = {
  enableHighAccuracy: true,
  timeout: 5000,
  maximumAge: 0,
};

// Fake [lat, lon] used in place of real GPS when running in the emulator. Switch
// debugCity to test a different region (menu header city, stop colors, etc.).
var debugLocations = {
  tampere: [61.50048576694305, 23.785473194189514],
  helsinki: [60.1699, 24.9384],
};
var debugCity = "tampere";
var debugLocation = debugLocations[debugCity];

// The emulator reports its model as "qemu_platform_<platform>"; real hardware
// reports a real model name (e.g. "pebble_black"). Use that to detect the emulator.
function isEmulator() {
  if (!Pebble.getActiveWatchInfo) {
    return false;
  }
  try {
    var model = Pebble.getActiveWatchInfo().model || "";
    return model.indexOf("qemu_") === 0;
  } catch (e) {
    return false;
  }
}

// Maps a Digitransit feed id (the part of a gtfsId before the colon) to the city
// label shown in the menu header. Only the regions we have themed so far are
// listed; any other feed leaves the header on its default ("Vuoro").
var feedToCity = {
  HSL: "Helsinki",
  tampere: "Tampere",
  Hameenlinna: "Hameenlinna",
  Joensuu: "Joensuu",
  LINKKI: "Jyvaskyla",
  Kotka: "Kotka",
  Kouvola: "Kouvola",
  Kuopio: "Kuopio",
  Lahti: "Lahti",
  Lappeenranta: "Lappeenranta",
  Mikkeli: "Mikkeli",
  OULU: "Oulu",
  Pori: "Pori",
  Raasepori: "Raasepori",
  Rovaniemi: "Rovaniemi",
  FOLI: "Turku",
  Vaasa: "Vaasa",
};

function cityFromGtfsId(gtfsId) {
  var feed = gtfsId.split(":")[0];
  return feedToCity[feed] || "";
}

// The watch only renders buses and trams (icons, badges, colors); every other
// mode is filtered out here so unsupported types never reach it.
function isSupportedMode(mode) {
  return mode === "BUS" || mode === "TRAM";
}

// Stop search parameters
const stopSearchDiameter = 500;
const stopsLimit = 10;

// Departure lines info parameters
const lineLimit = 10;

// Departure paging state. `departureStartTime` is the absolute epoch-seconds the
// currently shown window begins at (0 = "now"); `departureNextStartTime` is where
// the next "Show later" window should begin (just after the last departure we
// sent). The watch holds no extra departures, so paging never grows its memory.
var departureStartTime = 0;
var departureNextStartTime = 0;

function sendNextItem(items, index) {
  // Build message
  var dict = items[index];

  Pebble.sendAppMessage(
    dict,
    () => {
      // Use success callback to increment index
      index++;

      if (index < items.length) {
        sendNextItem(items, index);
      } else {
        Pebble.sendAppMessage({ messageEnd: 1 });
      }
    },
    () => {
      console.log("JS: Item transmission failed at index: " + index);
    }
  );
}

function sendList(items) {
  // An empty list still needs a terminating messageEnd so the watch can leave its
  // "Loading.." state and show an empty-state message. Without this, sendNextItem
  // would try to transmit items[0] (undefined) and never reach the end marker,
  // leaving the nearby-stops screen spinning forever when no stops are found.
  if (items.length === 0) {
    Pebble.sendAppMessage({ messageEnd: 1 });
    return;
  }
  var index = 0;
  sendNextItem(items, index);
}

function createGraphQLRequest(url) {
  var req = new XMLHttpRequest();
  req.open("POST", url, true);
  req.setRequestHeader("Content-Type", "application/graphql");
  req.setRequestHeader("digitransit-subscription-key", secrets.API_KEY);
  // A network-level failure (phone offline, DNS failure, timeout) fires onerror/
  // ontimeout rather than onload, so it is reported separately from a server that
  // responded but returned no results. The watch shows the no-internet screen.
  req.timeout = 8000;
  req.onerror = function () {
    console.log("JS: Network error — no internet connection.");
    Pebble.sendAppMessage({ noInternet: 1 });
  };
  req.ontimeout = function () {
    console.log("JS: Request timed out — no internet connection.");
    Pebble.sendAppMessage({ noInternet: 1 });
  };
  return req;
}

function getStopsFromLocation(pos) {
  var crd = pos.coords;
  var query = queries.createStopsQuery(crd.latitude, crd.longitude, stopSearchDiameter, 20);
  var req = createGraphQLRequest(url);

  req.onload = function (e) {
    if (req.readyState === 4) {
      if (req.status === 200) {
        if (req.responseText === "") {
          console.log("JS: Stops request returned no stops.");
          Pebble.sendAppMessage({ stopNoFound: 1 });
          return;
        }
        var response = JSON.parse(req.responseText);

        if (response && response.data && response.data.stopsByRadius) {
          var edges = response.data.stopsByRadius.edges;

          var stops = edges
            .filter(function (edge) {
              // Ignore results which code starts with MATKA. These
              // seem to be long range bus stops which do not interest us.
              // (MATKA stops are buses by mode, so the mode check below would
              // not exclude them on its own.)
              return (
                !edge.node.stop.gtfsId.startsWith("MATKA:") &&
                isSupportedMode(edge.node.stop.vehicleMode)
              );
            })
            .slice(0, stopsLimit)
            .map(function (edge) {
              return {
                stopMessage: 1,
                stopCode: edge.node.stop.gtfsId,
                stopName: edge.node.stop.name,
                stopDist: edge.node.distance,
                stopMode: edge.node.stop.vehicleMode,
              };
            });

          sendList(stops);
        } else {
          console.log("JS: No valid stops data in the GraphQL response.");
          Pebble.sendAppMessage({ stopNoFound: 1 });
        }
      } else {
        console.log(
          "JS: Error in getting stops from location. Status: " + req.status
        );
        Pebble.sendAppMessage({ stopNoFound: 1 });
      }
    }
  };

  req.send(query);
}

// Resolves the city the user is in from the feed prefix of the nearest stop and
// sends it to the watch for the menu header. The watch distinguishes two "no
// city" outcomes: a completed lookup that genuinely found no known city sends an
// empty cityName (the header clears, so it stays up to date), while a lookup that
// could not run at all (network/timeout/bad response) sends cityUnknown (the
// header keeps its last known city rather than blanking it over a transient
// hiccup). Neither surfaces the error screen used by the stops flow.
function getCityFromLocation(pos) {
  var crd = pos.coords;
  var query = queries.createCityQuery(crd.latitude, crd.longitude);
  var req = createGraphQLRequest(url);

  // These override the onerror/ontimeout set by createGraphQLRequest above.
  req.onerror = function () {
    console.log("JS: City lookup network error.");
    Pebble.sendAppMessage({ cityUnknown: 1 });
  };
  req.ontimeout = function () {
    console.log("JS: City lookup timed out.");
    Pebble.sendAppMessage({ cityUnknown: 1 });
  };

  req.onload = function () {
    if (req.readyState !== 4) {
      return;
    }
    if (req.status !== 200 || req.responseText === "") {
      console.log("JS: City lookup returned no data. Status: " + req.status);
      Pebble.sendAppMessage({ cityUnknown: 1 });
      return;
    }

    var response = JSON.parse(req.responseText);
    var edges =
      response && response.data && response.data.stopsByRadius
        ? response.data.stopsByRadius.edges
        : null;
    if (!edges) {
      console.log("JS: No valid city data in the GraphQL response.");
      Pebble.sendAppMessage({ cityUnknown: 1 });
      return;
    }

    var city = "";
    for (var i = 0; i < edges.length; i++) {
      var gtfsId = edges[i].node.stop.gtfsId;
      // Skip long-distance MATKA stops, as in the nearby-stops flow.
      if (gtfsId.indexOf("MATKA:") === 0) {
        continue;
      }
      city = cityFromGtfsId(gtfsId);
      break;
    }

    console.log("JS: Detected city: '" + city + "'");
    Pebble.sendAppMessage({ cityName: city });
  };

  req.send(query);
}

// Entry point for a cityMessage request: gets the user's location (debug
// location in the emulator) and resolves the surrounding city. A GPS failure
// reports cityUnknown so the header keeps its last known city rather than showing
// an error.
function handleCityDetection() {
  if (debugLocation && isEmulator()) {
    console.log("JS: Emulator detected, using debug location for city.");
    getCityFromLocation({
      coords: { latitude: debugLocation[0], longitude: debugLocation[1] },
    });
    return;
  }

  navigator.geolocation.getCurrentPosition(
    getCityFromLocation,
    function (err) {
      console.warn("JS: GPS error for city (" + err.code + "): " + err.message);
      // Location is unavailable, so we cannot determine the city: keep the last
      // known one (cityUnknown) rather than blanking the header, while still
      // letting it stop showing "Loading..".
      Pebble.sendAppMessage({ cityUnknown: 1 });
    },
    geolocationOptions
  );
}

// Fetches departures for a stop. `mode` selects the window:
//   "load"    - the stop's first ("now") window; resets the paging cursor.
//   "refresh" - re-fetch the window currently shown (the once-a-minute update),
//               keeping the user's place if they have paged forward.
//   "more"    - the next window, starting just after the last departure shown.
// When a "more" request finds nothing, `lineNoMore` is sent so the watch can
// keep the current list instead of blanking it like `lineNoFound` does.
function getDepartingLines(stopCode, mode) {
  if (mode === "load") {
    departureStartTime = 0;
  } else if (mode === "more") {
    departureStartTime = departureNextStartTime;
  }
  // "refresh" keeps departureStartTime as-is.

  var emptyMessage = mode === "more" ? { lineNoMore: 1 } : { lineNoFound: 1 };
  var query = queries.createDeparturesQuery(stopCode, departureStartTime, lineLimit);
  var req = createGraphQLRequest(url);

  req.onload = function (e) {
    if (req.readyState === 4) {
      if (req.status === 200) {
        if (req.responseText === "") {
          console.log("JS: Lines request returned no departing lines.");
          Pebble.sendAppMessage(emptyMessage);
          return;
        }
        var response = JSON.parse(req.responseText);

        if (response && response.data && response.data.stop) {
          var stop = response.data.stop;
          var stoptimes = stop.stoptimesWithoutPatterns;

          // Drop departures of unsupported modes (only buses and trams render on
          // the watch) before paging, so the window and the "show later" cursor
          // are computed from what the watch will actually show.
          if (stoptimes) {
            stoptimes = stoptimes.filter(function (st) {
              return isSupportedMode(st.trip.route.mode);
            });
          }

          if (!stoptimes || stoptimes.length === 0) {
            console.log("JS: No departing lines found for this window.");
            Pebble.sendAppMessage(emptyMessage);
            return;
          }

          // The fare zone and public stop code are stop-level properties, shown
          // on the departures screen. Sent ahead of the departures list (in the
          // same ordered transfer) so the watch can store them before the rows
          // arrive.
          var zone = stop.zoneId || "";
          var shortCode = stop.code || "";

          var departures = stoptimes.slice(0, lineLimit);

          // Remember where the next "Show later" window should begin: just after
          // the last departure in this one (absolute epoch seconds).
          var lastLine = departures[departures.length - 1];
          var lastSecs = lastLine.realtime
            ? lastLine.realtimeDeparture
            : lastLine.scheduledDeparture;
          departureNextStartTime = lastLine.serviceDay + lastSecs + 1;

          var lines = departures.map((line) => {
              // Use the realtime prediction when it is actually available;
              // otherwise realtimeDeparture just echoes the scheduled value.
              var isRealtime = line.realtime === true;
              var departureSecs = isRealtime
                ? line.realtimeDeparture
                : line.scheduledDeparture;
              return {
                lineMessage: 1,
                lineCode: line.trip.route.shortName,
                lineTime: convertSecondsToTime(departureSecs),
                lineDir: line.headsign,
                lineMode: line.trip.route.mode,
                lineRealtime: isRealtime ? 1 : 0,
                lineMins: minutesUntilDeparture(line.serviceDay, departureSecs),
            };
          });

          sendList([{ stopZone: zone, stopShortCode: shortCode }].concat(lines));
        } else {
          console.log("JS: No valid line data in the GraphQL response.");
          Pebble.sendAppMessage(emptyMessage);
        }
      } else {
        console.log(
          "JS: Error in getting lines for stop. Status: " + req.status
        );
        Pebble.sendAppMessage(emptyMessage);
      }
    }
  };

  req.send(query);
}

// Great-circle distance in whole meters between two lat/lon points.
function distanceMeters(lat1, lon1, lat2, lon2) {
  var R = 6371000;
  var toRad = function (deg) {
    return (deg * Math.PI) / 180;
  };
  var dLat = toRad(lat2 - lat1);
  var dLon = toRad(lon2 - lon1);
  var a =
    Math.sin(dLat / 2) * Math.sin(dLat / 2) +
    Math.cos(toRad(lat1)) *
      Math.cos(toRad(lat2)) *
      Math.sin(dLon / 2) *
      Math.sin(dLon / 2);
  return Math.round(R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a)));
}

// Resolves the pinned stops to name/mode/coordinates, computes the distance
// from the user's position (when known), and sends them to the watch ordered
// nearest-first. When position is null the list is sent in its original order
// with no distance.
function getPinnedStops(codes, lat, lon) {
  var query = queries.createPinnedStopsQuery(codes);
  var req = createGraphQLRequest(url);

  req.onload = function (e) {
    if (req.readyState !== 4) {
      return;
    }
    if (req.status !== 200 || req.responseText === "") {
      console.log("JS: Error loading pinned stops. Status: " + req.status);
      Pebble.sendAppMessage({ pinNoFound: 1 });
      return;
    }

    var response = JSON.parse(req.responseText);
    if (!response || !response.data || !response.data.stops) {
      console.log("JS: No valid pinned stops data in the GraphQL response.");
      Pebble.sendAppMessage({ pinNoFound: 1 });
      return;
    }

    var hasLocation = lat !== null && lon !== null;
    var stops = response.data.stops
      .filter(function (stop) {
        // Unknown ids come back as null; drop them. Also drop any pinned stop
        // whose mode the watch cannot render (only buses and trams).
        return stop != null && isSupportedMode(stop.vehicleMode);
      })
      .map(function (stop) {
        return {
          code: stop.gtfsId,
          name: stop.name,
          mode: stop.vehicleMode,
          dist: hasLocation
            ? distanceMeters(lat, lon, stop.lat, stop.lon)
            : -1,
        };
      });

    if (hasLocation) {
      stops.sort(function (a, b) {
        return a.dist - b.dist;
      });
    }

    if (stops.length === 0) {
      Pebble.sendAppMessage({ pinNoFound: 1 });
      return;
    }

    var items = stops.map(function (stop) {
      return {
        pinMessage: 1,
        pinCode: stop.code,
        pinName: stop.name,
        pinDist: stop.dist,
        pinMode: stop.mode,
      };
    });
    sendList(items);
  };

  req.send(query);
}

// Entry point for a pinMessage request: parses the comma-separated codes, gets
// the user's location, and fetches the pinned stops.
function handlePinnedStops(csv) {
  var codes = csv.split(",").filter(function (code) {
    return code.length > 0;
  });
  if (codes.length === 0) {
    Pebble.sendAppMessage({ messageEnd: 1 });
    return;
  }

  if (debugLocation && isEmulator()) {
    console.log("JS: Emulator detected, using debug location for pinned stops.");
    getPinnedStops(codes, debugLocation[0], debugLocation[1]);
    return;
  }

  navigator.geolocation.getCurrentPosition(
    function (pos) {
      getPinnedStops(codes, pos.coords.latitude, pos.coords.longitude);
    },
    function (err) {
      console.warn("JS: GPS error (" + err.code + "): " + err.message);
      // Still show pinned stops, just unsorted and without distances.
      getPinnedStops(codes, null, null);
    },
    geolocationOptions
  );
}

Pebble.addEventListener("ready", function (e) {
  console.log("JS: Javascript component ready");
  // Tell the watch the JS side is up so it can fire its startup city lookup now.
  // A request sent before this point (e.g. straight from app init, while the
  // splash shows) would be dropped because this component is not loaded yet.
  Pebble.sendAppMessage({ jsReady: 1 });
});

function convertSecondsToTime(seconds) {
  // Digitransit reports departures as seconds past midnight of the service day,
  // so a trip running after midnight exceeds 24h (e.g. 87300 = "24:15"). Wrap the
  // hour into 0-23 so it shows as a real clock time (00:15).
  const hours = Math.floor(seconds / 3600) % 24;
  const minutes = Math.floor((seconds % 3600) / 60);

  // Pad hours and minutes with leading zeros if needed
  const paddedHours = hours.toString().padStart(2, "0");
  const paddedMinutes = minutes.toString().padStart(2, "0");

  return `${paddedHours}:${paddedMinutes}`;
}

// Whole minutes from now until a departure. serviceDay is the epoch-seconds of
// the service date's midnight, departureSecs is seconds past that midnight, so
// their sum is the absolute departure time (robust across midnight).
function minutesUntilDeparture(serviceDay, departureSecs) {
  var nowSecs = Date.now() / 1000;
  var departureEpoch = serviceDay + departureSecs;
  return Math.floor((departureEpoch - nowSecs) / 60);
}

Pebble.addEventListener("appmessage", function (e) {
  if (e.payload.stopMessage) {
    console.log("JS: Received stopMessage.");
    if (debugLocation && isEmulator()) {
      console.log("JS: Emulator detected, using debug location: " + debugLocation);
      getStopsFromLocation({
        coords: { latitude: debugLocation[0], longitude: debugLocation[1] },
      });
      return;
    }
    navigator.geolocation.getCurrentPosition(
      getStopsFromLocation,
      (err) => {
        console.warn("JS: GPS error (" + err.code + "): " + err.message);
        Pebble.sendAppMessage({ noGps: 1 });
      },
      geolocationOptions
    );
  } else if (e.payload.lineMessage) {
    console.log(
      "JS: Received lineMessage with stopCode: " + e.payload.lineMessage
    );
    getDepartingLines(e.payload.lineMessage, "load");
  } else if (e.payload.lineRefresh) {
    console.log(
      "JS: Received lineRefresh with stopCode: " + e.payload.lineRefresh
    );
    getDepartingLines(e.payload.lineRefresh, "refresh");
  } else if (e.payload.lineMore) {
    console.log(
      "JS: Received lineMore with stopCode: " + e.payload.lineMore
    );
    getDepartingLines(e.payload.lineMore, "more");
  } else if (e.payload.cityMessage) {
    console.log("JS: Received cityMessage.");
    handleCityDetection();
  } else if (typeof e.payload.pinMessage !== "undefined") {
    console.log("JS: Received pinMessage with codes: " + e.payload.pinMessage);
    handlePinnedStops(e.payload.pinMessage);
  } else {
    console.log("JS: Received unknown message from Pebble!");
  }
});
