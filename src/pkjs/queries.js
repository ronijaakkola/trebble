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

// Resolves the surrounding city/region by finding the nearest stops and reading
// the feed prefix of a gtfsId (e.g. "HSL:..." -> Helsinki). Only the id is
// needed. A few results are fetched so long-distance "MATKA" stops can be
// skipped, and a wide radius makes detection work even where the nearest local
// stop is beyond the normal nearby-stops range.
//
// The same request also checks whether the city has city bikes (any rental station
// within 5 km — the radius that, per measurement, reaches a station from essentially
// any in-city/suburb location). The watch uses this to hide the "City bikes" menu row
// in regions with no bike system. Folding it into the city query avoids a second
// network round-trip.
function createCityQuery(latitude, longitude) {
  return `
{
  stopsByRadius(lat: ${latitude}, lon: ${longitude}, radius: 5000, first: 5) {
    edges {
      node {
        stop {
          gtfsId
        }
      }
    }
  }
  bikeCheck: nearest(lat: ${latitude}, lon: ${longitude}, maxDistance: 5000, maxResults: 1, filterByPlaceTypes: [VEHICLE_RENT]) {
    edges {
      node {
        place {
          __typename
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

// Finds city bike / bike-share stations near a location. Digitransit has no
// "...ByRadius" query for rental stations, so the generic `nearest` query is used
// with filterByPlaceTypes: [VEHICLE_RENT]; it returns edges carrying a walking
// `distance` (meters) and a `place`, mirroring stopsByRadius. `place` is a union, so
// a VehicleRentalStation inline fragment selects the station fields. The filter can
// also match free-floating RentalVehicles, so the JS filters on __typename. Only the
// available-bikes count is shown on the watch (availableVehicles.total); availableSpaces
// is intentionally not requested (the list shows bikes only).
function createBikeStationsQuery(latitude, longitude, radius, limit) {
  return `
{
  nearest(lat: ${latitude}, lon: ${longitude}, maxDistance: ${radius}, maxResults: ${limit}, filterByPlaceTypes: [VEHICLE_RENT]) {
    edges {
      node {
        distance
        place {
          __typename
          ... on VehicleRentalStation {
            stationId
            name
            operative
            realtime
            availableVehicles {
              total
            }
          }
        }
      }
    }
  }
}`;
}

module.exports = {
  createStopsQuery,
  createCityQuery,
  createDeparturesQuery,
  createPinnedStopsQuery,
  createBikeStationsQuery
};