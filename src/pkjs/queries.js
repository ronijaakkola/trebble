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

function createDeparturesQuery(stopCode) {
  return `
{
  stop(id: "${stopCode}") {
    name
    stoptimesWithoutPatterns(omitNonPickups: true, numberOfDepartures: 10) {
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