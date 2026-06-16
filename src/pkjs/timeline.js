// Minimal timeline helper: pushes a single *user* pin to the timeline for the
// current user via the public timeline web API. This is the supported way for an
// app to add a local pin — a sideloaded app gets a sandbox token automatically
// (no app-store publishing or own backend needed). See
// src/pkjs/app.js (handleAddToTimeline) for the caller, and the project's
// add-to-timeline notes for the local-vs-server investigation.

// Host the pin PUT is sent to. The user token returned by getTimelineToken (the
// phone/Pebble app handles that) must be issued by the same timeline service, so
// if pins don't appear on hardware this is the first thing to check/adjust.
// (The legacy timeline-api.getpebble.com host is dead; Rebble's live host is used.)
var API_URL_ROOT = 'https://timeline-api.rebble.io/';

// Issues the pin request once a user timeline token is available. `callback` is
// called with (err, body): err is null on success, otherwise a short reason.
function timelineRequest(pin, method, callback) {
  Pebble.getTimelineToken(
    function (token) {
      var url = API_URL_ROOT + 'v1/user/pins/' + pin.id;
      var xhr = new XMLHttpRequest();
      xhr.onload = function () {
        if (xhr.status >= 200 && xhr.status < 300) {
          callback(null, xhr.responseText);
        } else {
          callback('HTTP ' + xhr.status, xhr.responseText);
        }
      };
      xhr.onerror = function () { callback('network error'); };
      xhr.ontimeout = function () { callback('timed out'); };
      xhr.open(method, url);
      xhr.setRequestHeader('Content-Type', 'application/json');
      xhr.setRequestHeader('X-User-Token', '' + token);
      xhr.timeout = 10000;
      xhr.send(JSON.stringify(pin));
    },
    function (error) {
      callback('no timeline token: ' + error);
    }
  );
}

// Inserts (or, with the same id, updates) a pin in the user's timeline.
module.exports.insertUserPin = function (pin, callback) {
  timelineRequest(pin, 'PUT', callback);
};

// Removes a previously inserted pin by id.
module.exports.deleteUserPin = function (pin, callback) {
  timelineRequest(pin, 'DELETE', callback);
};
