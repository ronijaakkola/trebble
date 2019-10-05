![trebble](https://github.com/ronijaakkola/trebble/blob/master/screenshots/banner.png)

# NOTE
**As of October 2019, the current version the city of Tampere has depricated Tampere HTTP GET API (http://api.publictransport.tampere.fi/prod/) and suggest using Digitransit APIs (https://digitransit.fi/en/developers/) instead. There is no backwards compatibility and the API did stop working immediately after the annoucement. Currently this application does not work since it did rely on the old HTTP GET API.**

# About
Trebble is a Pebble smart watch app for finding the next departing busses in Tampere, Finland. It uses GPS location to find your nearest stops and shows the next lines departing from those stops. This source supports all currently available Pebble models, including the latest Pebble 2 which was the last Pebble smart watch released before Fitbit acquired Pebble.

# Features
**Find nearby stops**

Trebble uses your phone's GPS to locate the nearest bus stops for you. Trebble automatically finds 10 most nearest stops. In the stop view the stops are shown in order from nearest to farthest. The stop name is shown as well as the distance to the stop. Since the GPS accuracy can vary between locations and phone models, it is important to show the distance to the user so he can determine the corresponding stop.

**See the next departing lines**

When the stop is chosen, Trebble shows the next departing lines from that stop. By default, Trebble shows the next ten departing lines. From a particular line, the line number and destination is shown. Most importantly, the departure time is shown for each line. Note! Trebble does not use the real time data provided by the Tampere bus API. Because of this it is possible, that the departure times are not always accurate.

# Usage
This app is licenced under a MIT license. You can use it to do pretty much anything you want. The easiest way to modify and build the code is to download the whole source and open it in Pebble's [CloudPebble](https://cloudpebble.net/) platform. Take note that the icons are downloaded from Freepik (see the README file in the resources/images folder).

# Known issues
**Times are sometimes incorrect during night time**

Some users have reported that bus departure times can be incorrect during night time. For example, 01:00 (AM) can show as 25:00. This is a bug in the Tampere bus API which has been reported to the API maintainer.
