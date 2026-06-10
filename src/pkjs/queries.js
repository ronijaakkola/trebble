function createStopsQuery(latitude, longitude, radius, limit) {
  return `
{
  stopsByRadius(lat: ${latitude}, lon: ${longitude}, radius: ${radius}, first: ${limit}) {
    edges {
      node {
        stop {
          gtfsId
          name
          vehicleMode
        }
        distance
      }
    }
  }
}`;
}

function createDeparturesQuery(stopCode, startTime, numberOfDepartures) {
  // startTime is absolute epoch seconds; 0 means "now", in which case it is
  // omitted so the API defaults to the current time. A wide timeRange lets the
  // "Show later" windows (which start well into the future) still return a full
  // page of departures rather than being cut off by a short default window.
  var startArg = startTime && startTime > 0 ? `, startTime: ${startTime}` : "";
  return `
{
  stop(id: "${stopCode}") {
    name
    code
    zoneId
    stoptimesWithoutPatterns(omitNonPickups: true, numberOfDepartures: ${numberOfDepartures}${startArg}, timeRange: 604800) {
      scheduledDeparture
      realtimeDeparture
      realtime
      serviceDay
      headsign
      trip {
        route {
          shortName
          mode
        }
      }
    }
  }
}`;
}

// Looks up a set of stops by their gtfsId so their coordinates (and current
// name/mode) can be resolved for the pinned stops list. Digitransit returns the
// stops in the order of the requested ids, with null for any id it cannot find.
function createPinnedStopsQuery(codes) {
  var idList = codes
    .map(function (code) {
      return '"' + code + '"';
    })
    .join(", ");
  return `
{
  stops(ids: [${idList}]) {
    gtfsId
    name
    lat
    lon
    vehicleMode
  }
}`;
}

module.exports = {
  createStopsQuery,
  createDeparturesQuery,
  createPinnedStopsQuery
};