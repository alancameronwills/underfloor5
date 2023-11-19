#include "outside.h"
#include <RTCZero.h>
#include "logger.h"
#include "utility.h"
#include "webclient.h"
#include "parameters.h"

extern float avgDeficit;
extern bool logging;
extern RTCZero rtc;

/*
   Weather

*/

bool Weather::getWeather() {
  String msg = "";
  if (getWeatherForecast(msg) && parseWeather(msg)) {
    avgDeficit = getForecastTempDiff(targetTemp);
    return true;
  }
  else return false;
}

bool Weather::getWeatherForecast(String& msg)
{
  const char* requestedLocation = "353070";
  const String weatherReq = String("/public/data/val/wxfcs/all/json/") + requestedLocation + "?res=daily&key=75eea32c-ec7b-499f-9d47-a1ab760bf8da";
  return getWeb ((char*)"datapoint.metoffice.gov.uk", 80, weatherReq, "", msg);
}


String WeatherDay::report () {
  return day(fcDate) + " " + TwoDigits(tempMax) + ".." + TwoDigits(tempMin) + " " + ThreeChars(windDirection) + TwoDigits(windSpeed) + " " + TwoDigits(precip) + "% " + weather + "\n";
};


String Weather::weatherReport() {
  String w = location + "\n";
  for (int i = 0; i < WEATHER_DAYS; i++) {
    if (forecast[i].fcDate.length() > 0) {
      w += forecast[i].report();
    }
  }
  return w;
}

bool Weather::parseWeather(String& msg)
{
  //clogn("Parse weather ");
  location = "";
  for (int i = 0; i < WEATHER_DAYS ; i++) {
    forecast[i].fcDate = "";
  }

  int msgix = 0;
  const String dateProlog = "\"type\":\"Day\",\"value\":\"";

  msgix = msg.indexOf("\"Location\":");
  location = getProp(msg, "name", msgix, msgix + 10000);

  if (location == "") return false;

  //clogn(location);

  String report = location + "\n";

  bool gotLines = false;

  for (int i = 0; i < WEATHER_DAYS ; i++)  {
    WeatherDay& cw = forecast[i];
    msgix = msg.indexOf(dateProlog, msgix + 1);
    if (msgix < 0) break;
    gotLines = true;
    cw.fcDate = msg.substring(msgix + dateProlog.length(), msgix + dateProlog.length() + 10);
    int endSegmentIx = msg.indexOf(']', msgix);
    cw.tempMin = getProp(msg, "Nm", msgix, endSegmentIx);
    cw.tempMax = getProp(msg, "Dm", msgix, endSegmentIx);
    cw.windDirection = getProp(msg, "D", msgix, endSegmentIx);
    cw.windSpeed = getProp(msg, "S", msgix, endSegmentIx);
    cw.precip = getProp(msg, "PPd", msgix, endSegmentIx);
    cw.precipN = getProp(msg, "PPn", msgix, endSegmentIx);
    String weatherCode = getProp(msg, "W", msgix, endSegmentIx);
    cw.weather = code(weatherCode.toInt());
    report += cw.report();
  }
  clogn(report + "===");
  return gotLines;
}



float Weather::getForecastTempDiff(float targetTemp) {
  const float invAvgFactor = 1.0 - avgFactor;
  float avgTempDiff = 200;
  //int isAfternoon = rtc.getHours() > 12 ? 1 : 0;
  for (int i = WEATHER_DAYS - 1;  i >= 0; i--)
  {
    if (forecast[i].fcDate.length() > 0) {
      float mint = forecast[i].tempMin.toFloat();
      float maxt = forecast[i].tempMax.toFloat();
      //clog (String("") + mint + ".." + maxt + " |");
      float diff = targetTemp - (forecast[i].tempMin.toFloat() * 2 + forecast[i].tempMax.toFloat()) / 3;
      if (avgTempDiff > 100) avgTempDiff = diff;
      else avgTempDiff = avgTempDiff * invAvgFactor + diff * avgFactor;
    }
  }
  float adjustedDeficit = avgTempDiff;
  int windSpeed = forecast[0].windSpeed.toInt();
  if (windSpeed > 0 && windSpeed < 100) {
    adjustedDeficit *= 1.0 + windSpeed * windSpeedFactor / 100;
  }
  dlogn(String("Deficit: ") + avgTempDiff + " Wind: " + windSpeed + " Adjusted deficit: " + adjustedDeficit);
  File f = SD.open("DEFICIT.TXT", FILE_REWRITE);
  if (f) {
    f.println(adjustedDeficit);
    f.close();
  }
  return adjustedDeficit;
}



String Weather::codes[30] =
    { "stars", "sun", "cloud", "cloud", "", "mist", "fog", "Cloud", "ocast", "shwr", "shwr", "drizl",
      "rain", "Shwr", "Shwr", "RAIN",
      "sleet", "sleet", "Sleet", "hail", "hail",
      "snow", "snow", "snow", "Snow", "Snow", "SNOW",
      "thndr", "thndr", "Thndr"
    };


/**** Tidal ********/


String Tidal::tidesReport() {
  String dst = isSummertime() ? "BST" : "GMT";
  String r;
  for (int i = 0; i < 4; i++) {
    Tide &t = tides[i];
    if (t.eventType.length() > 0) {
      r += String(String(t.eventType).substring(0, 2) + " " + d2(t.day) + " " + d2(int(t.tod)) + ":" + d2(int(t.tod * 60) % 60) + " " + dst + " " + t.height + "\n");
    }
  }
  return r;
}

bool Tidal::getSunMoon() {
  bool success = getSunMoon("sun", sunRise, sunSet) && getSunMoon("moon", moonRise, moonSet);
  if (success && moonRise > moonSet) moonSet += 1.1; // show tomorrow's setting
  return success;
}

bool Tidal::getSunMoon(String sunOrMoon, float &riseHour, float &setHour) {
  String s;
  String req = "/weatherapi/sunrise/3.0/{3}?lat=52.07&lon=-4.75&date=20{0}-{1}-{2}&offset=+00:00";
  req.replace("{0}", d2(rtc.getYear()));
  req.replace("{1}", d2(rtc.getMonth()));
  req.replace("{2}", d2(rtc.getDay()));
  req.replace("{3}", sunOrMoon);
  if (getWeb((char*)"api.met.no", 443, req, "", s)) {
    return extractRiseSet(s, sunOrMoon + "rise", riseHour)
           && extractRiseSet(s, sunOrMoon + "set", setHour);
  }
  else return false;
}


bool Tidal::extractRiseSet (String s, String id, float &hour) {
  int bodyStart = s.indexOf(id);
  if (bodyStart < 0) return false;
  int tStart = s.indexOf("T", bodyStart);
  String hourString = s.substring(tStart + 1, tStart + 3);
  String minuteString = s.substring(tStart + 4, tStart + 6);
  hour = hourString.toInt() + (0.0 + minuteString.toInt()) / 60.0;
  clogn(String(id) + " " + hour);
  return true;
}



bool Tidal::getTides()
{
  for (int i = 0; i < 4; i++) tides[i].eventType = "";
  String tideResponse = "";
  const String tideReq = String("/Home/GetPredictionData?stationId=0490");
  if (getWeb((char*)"easytide.admiralty.co.uk", 443, tideReq, "", tideResponse)) {
    dlogn("Got tides");
    return parseTides(tideResponse);
  }
  else return false;
}


bool Tidal::parseTides(String &msg)
{
  int mix = 0;
  bool current = false; // skip tides earlier than the most recent
  int cday = rtc.getDay();
  int chour = rtc.getHours();
  int tix = 0;
  bool summer = isSummertime();
  while (tix < 4) {
    Tide &t = tides[tix];
    int endSegment = msg.indexOf("}", mix);
    if (endSegment < 0) {
      clogn("endsegment");
      break;
    }
    t.eventType = getProp(msg, "eventType", mix, endSegment);
    String date = getProp(msg, "dateTime", mix, endSegment);
    //clog(date + "  ");
    t.height = getProp(msg, "height", mix, endSegment).toFloat();
    mix = endSegment + 1;
    if (date.length() < 16) {
      clogn(date + "-no date");
      continue;
    }
    t.day = date.substring(8, 10).toInt();
    t.tod = date.substring(11, 13).toFloat() + date.substring(14, 16).toFloat() / 60 + (summer ? 1.0 : 0);
    if (t.tod >= 24.0) {
      t.tod -= 24.0;
      t.day++;
    }
    //clogn(date + ">" + t.day + "T" + t.tod);
    if (t.day == cday && t.tod > chour - 6 || t.day > cday) current = true; // start collecting
    if (current) tix++;
  }
  if (logging) {
    String report = "";
    for (int tix = 0; tix < 4; tix++) {
      report += tides[tix].eventType + " " + tides[tix].day + "T" + tides[tix].tod + " " + tides[tix].height + "\n";
    }
    clogn(report);
  }
  return tix >= 2;
}
