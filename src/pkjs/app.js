# TODO: Old HTTP GET API has been depricated. We should start using Digitransit API (https://digitransit.fi/en/developers/)

var apiKey = "";
var apiPassphrase = "";

var options = {
  enableHighAccuracy: true,
  timeout: 5000,
  maximumAge: 0
};

// Stop search parameters
var stopSearchDiameter = 500;
var stopsLimit = 8;

// Stop info parameters
var depLimit = 10;
var timeLimit = 360;

function sendNextItem(items, index) {
  // Build message
  var dict = items[index];

  // Send the message
  Pebble.sendAppMessage(dict, function() {
    // Use success callback to increment index
    index++;

    if(index < items.length) {
      // Send next item
      sendNextItem(items, index);
    } else {
			Pebble.sendAppMessage({ messageEnd: 1 });
    }
  }, function() {
		console.log('JS: Item transmission failed at index: ' + index);
  });
}

function sendList(items) {
	var index = 0;
  sendNextItem(items, index);
}

function parseStops(json) 
{
	var stops = [];
	for (var i = 0; i < json.length; ++i) {
		var stop = json[i];
		stops.push({
			'stopMessage': 1,
			'stopCode': stop.code,
			'stopName': stop.name,
			'stopDist': stop.dist
		});
	}
	return stops;
}

function parseLines(json) 
{
	var lines = [];
	for (var i = 0; i < json.length; ++i) {
		var line = json[i];
		var time = line.time.substring(0,2) + ':' + line.time.substring(2);
		var dir = line.name1.split(" - ")[1];
		lines.push({
			'lineMessage': 1,
			'lineCode': line.code,
			'lineTime': time,
			'lineDir': dir
		});
	}
	return lines;
}

# TODO: Old HTTP GET API has been depricated. We should start using Digitransit API (https://digitransit.fi/en/developers/)
# NOTE: Data format Digitransit uses is vastly different from the old APIs.
function getStopsFromLocation(pos) 
{
  var response;
	var crd = pos.coords;
	# NOTE: Depricated API
	var url = "http://api.publictransport.tampere.fi/prod/?request=stops_area&user=" + apiKey + "&pass=" + apiPassphrase + "&epsg_in=wgs84&center_coordinate=" + crd.longitude + "," + crd.latitude + "&limit=" + stopsLimit + "&diameter=" + stopSearchDiameter;
	
  var req = new XMLHttpRequest();
  req.open('GET', url, true);
  req.onload = function(e) {
  	if (req.readyState == 4) {
    	if (req.status == 200) {
				if (req.responseText === "") {
					console.log("JS: Stops request returned no stops.");
					Pebble.sendAppMessage({ stopNoFound: 1 });
					return;
				}
        response = JSON.parse(req.responseText);
        if (response) {
					sendList(parseStops(response));
				}
    	}
    	else {
				console.log("JS: Error in getting stops from location.");
				Pebble.sendAppMessage({ stopNoFound: 1 });
    	}
    }
  };
	req.send(null);
}

# TODO: Old HTTP GET API has been depricated. We should start using Digitransit API (https://digitransit.fi/en/developers/)
# NOTE: Data format Digitransit uses is vastly different from the old APIs.
function getDepartingLines(stopCode) 
{
	var response;
	# NOTE: Depricated API
  	var url = "http://api.publictransport.tampere.fi/prod/?request=stop&user=" + apiKey + "&pass=" + apiPassphrase + "&code=" + stopCode + "&time_limit=" + timeLimit + "&dep_limit=" + depLimit;
	
  var req = new XMLHttpRequest();
  req.open('GET', url, true);
  req.onload = function(e) {
  	if (req.readyState == 4) {
    	if (req.status == 200) {
				if (req.responseText === "") {
					console.log("JS: Lines request returned no departing lines.");
					Pebble.sendAppMessage({ lineNoFound: 1 });
					return;
				}
        response = JSON.parse(req.responseText);
        if (response) {
					if (response[0].departures === "") {
						console.log("JS: Lines request returned no departing lines.");
						Pebble.sendAppMessage({ lineNoFound: 1 });
						return;
					}
					sendList(parseLines(response[0].departures));
				}
			}
    }
    else {
			console.log("JS: Error in getting lines of stop.");
    }
  };
	req.send(null);
}

Pebble.addEventListener("ready", function(e) {
	console.log("Javascript component ready");								
});

function error(err) {
	console.warn('JS: GPS error (' + err.code + '): ' + err.message);
	Pebble.sendAppMessage({ noGps: 1 });
}

Pebble.addEventListener("appmessage", function(e) {
	if (e.payload.stopMessage) {
		console.log("JS: Received stopMessage.");
		navigator.geolocation.getCurrentPosition(getStopsFromLocation, error, options);
	}
	else if (e.payload.lineMessage) {
		console.log("JS: Received lineMessage with stopCode: " + e.payload.lineMessage);
		getDepartingLines(e.payload.lineMessage);
	}
	else {
		console.log("JS: Received unknown message from Pebble!");
	}
});
