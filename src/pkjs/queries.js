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

module.exports = {
  createStopsQuery,
  createDeparturesQuery
}; 