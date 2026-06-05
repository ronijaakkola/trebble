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
  } else {
    console.log("JS: Received unknown message from Pebble!");
  }
});
