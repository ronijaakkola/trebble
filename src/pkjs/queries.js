function createStopsQuery(latitude, longitude, radius, limit) {
  return `
{
  stopsByRadius(lat: ${latitude}, lon: ${longitude}, radius: ${radius}, first: ${limit}) {
    edges {
      node {
        stop {
          gtfsId
          name
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
    stoptimesWithoutPatterns(omitNonPickups: true) {
      scheduledDeparture
      headsign
      trip {
        route {
          shortName
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