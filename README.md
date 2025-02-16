![trebble](https://github.com/ronijaakkola/trebble/blob/master/screenshots/banner.png)

# NOTE
If you have not heard the news, [Pebble is back](https://repebble.com/)! To celebrate that, this project will be soon be updated back to working order. 

# About
Trebble is a Pebble smart watch app for finding the next departing busses in Tampere, Finland. It uses GPS location to find your nearest stops and shows the next lines departing from those stops. This source supports all currently available Pebble models, including the latest Pebble 2 which was the last Pebble smart watch released before Fitbit acquired Pebble.

# Features
**Find nearby stops**

Trebble uses your phone's GPS to locate the nearest bus stops for you. Trebble automatically finds 10 most nearest stops. In the stop view the stops are shown in order from nearest to farthest. The stop name is shown as well as the distance to the stop. Since the GPS accuracy can vary between locations and phone models, it is important to show the distance to the user so he can determine the corresponding stop.

**See the next departing lines**

When the stop is chosen, Trebble shows the next departing lines from that stop. By default, Trebble shows the next ten departing lines. From a particular line, the line number and destination is shown. Most importantly, the departure time is shown for each line. Note! Trebble does not use the real time data provided by the Tampere bus API. Because of this it is possible, that the departure times are not always accurate.

# Known issues
**Times are sometimes incorrect during night time**

Some users have reported that bus departure times can be incorrect during night time. For example, 01:00 (AM) can show as 25:00. This is a bug in the Tampere bus API which has been reported to the API maintainer.
