const secrets = require("./secrets");
const queries = require("./queries");

var url = "https://api.digitransit.fi/routing/v2/finland/gtfs/v1";

var geolocationOptions = {
  enableHighAccuracy: true,
  timeout: 5000,
  maximumAge: 0,
};

// Fake [lat, lon] used in place of real GPS when running in the emulator
var debugLocation = [61.50048576694305, 23.785473194189514];

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

// Stop search parameters
const stopSearchDiameter = 500;
const stopsLimit = 10;

// Departure lines info parameters
const lineLimit = 10;

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
              return !edge.node.stop.gtfsId.startsWith("MATKA:");
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

function getDepartingLines(stopCode) {
  var query = queries.createDeparturesQuery(stopCode);
  var req = createGraphQLRequest(url);

  req.onload = function (e) {
    if (req.readyState === 4) {
      if (req.status === 200) {
        if (req.responseText === "") {
          console.log("JS: Lines request returned no departing lines.");
          Pebble.sendAppMessage({ lineNoFound: 1 });
          return;
        }
        var response = JSON.parse(req.responseText);

        if (response && response.data && response.data.stop) {
          var stop = response.data.stop;
          var stoptimes = stop.stoptimesWithoutPatterns;

          if (!stoptimes || stoptimes.length === 0) {
            console.log("JS: No departing lines found for this stop.");
            Pebble.sendAppMessage({ lineNoFound: 1 });
            return;
          }

          var lines = stoptimes
            .slice(0, lineLimit)
            .map((line) => {
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

          sendList(lines);
        } else {
          console.log("JS: No valid line data in the GraphQL response.");
          Pebble.sendAppMessage({ lineNoFound: 1 });
        }
      } else {
        console.log(
          "JS: Error in getting lines for stop. Status: " + req.status
        );
        Pebble.sendAppMessage({ lineNoFound: 1 });
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
        // Unknown ids come back as null; drop them.
        return stop != null;
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
});

function convertSecondsToTime(seconds) {
  const hours = Math.floor(seconds / 3600);
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
    getDepartingLines(e.payload.lineMessage);
  } else if (typeof e.payload.pinMessage !== "undefined") {
    console.log("JS: Received pinMessage with codes: " + e.payload.pinMessage);
    handlePinnedStops(e.payload.pinMessage);
  } else {
    console.log("JS: Received unknown message from Pebble!");
  }
});
