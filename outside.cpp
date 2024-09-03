#include "outside.h"
#include <RTCZero.h>
#include "logger.h"
#include "utility.h"
#include "webclient.h"
#include "parameters.h"
#include <ctime>

extern bool logging;
extern RTCZero rtc;
extern Tidal tidal;

/*
   Weather

*/

bool getWeatherCache(String &response, unsigned long &timestamp)
{
  String bb = getShortFileContent("WEATHER.TXT");
  if (bb.length() > 0)
  {
    timestamp = bb.toInt();
    long now = rtcSeconds();
    if (now > timestamp && timestamp > 0 && now - timestamp < 36000 /*10h*/)
    {
      response = bb;
      return true;
    }
  }
  return false;
}
bool saveWeatherCache(String response)
{
  File weatherCache = SD.open("WEATHER.TXT", FILE_REWRITE);
  if (weatherCache)
  {
    weatherCache.println(rtcSeconds());
    weatherCache.println(response);
    weatherCache.close();
    return true;
  }
  return false;
}

/** public

*/
bool Weather::useWeatherAsync(void (*_gotWeather)(Weather *))
{
  gotWeather = _gotWeather;
  if (weatherAge() == 0)
  {
    // No valid forecast in memory; first try cache:
    unsigned long timestamp = 0;
    String response;
    if (getWeatherCache(/*&*/ response, /*&*/ timestamp))
    {
      // Sets weatherAge and forecast if the content makes sense:
      parseWeather(response, timestamp);
    }
  }
  // weatherAge is possibly updated to cache age
  if (weatherAge() != 0 && weatherAge() < 10)
  {
    clogn("Using cached weather");
    gotWeather(this); // use cached weather that's not too old
    return true;
  }
  else
  {
    if (!getWeatherForecastAsync())
    {
      if (weatherAge() != 0)
      {
        gotWeather(this); // use old cache because we can't get fresh
      }
      return false; // try again later
    }
    return true; // hope to receive forecast
  }
}

/** Hours since real forecast obtained, or 0 == never == forcast invalid*/
int Weather::weatherAge()
{
  if (forecastTimestamp == 0)
    return 0;
  return (rtcSeconds() - forecastTimestamp) / 3600;
}

/*

   https://register.metoffice.gov.uk/MyAccountClient/account/view

bool Weather::getWeatherForecastAsync()
{
  const char* requestedLocation = "353070";
  const String weatherReq = String("/public/data/val/wxfcs/all/json/") + requestedLocation + "?res=daily&key=d8ebf147-4cd1-4824-aac9-635e600f9ba5";
  //return getWeb ((char*)"datapoint.metoffice.gov.uk", 80, weatherReq, "", response);
  return getWebAsync((char*)"datapoint.metoffice.gov.uk", 80, weatherReq, "", this, 1000);
}
*/

/* https://open-meteo.com/ */
bool Weather::getWeatherForecastAsync()
{
  const String weatherReq = String("/v1/forecast?latitude=52.06&longitude=-4.75&hourly=weather_code,temperature_2m,precipitation_probability,wind_speed_10m,wind_direction_10m&timezone=auto");
  return getWebAsync((char *)"api.open-meteo.com", 443, weatherReq, "", this, 1000);
}

void Weather::gotResponse(int status, String response)
{
  if (parseWeather(response, rtcSeconds()))
  {
    saveWeatherCache(response);
    gotWeather(this);
  }
}

int days(long int seconds)
{
  return seconds / (3600 * 24);
}
int years(long int seconds)
{
  return 1970 + days(seconds) / 365;
}
int dayOfWeek(long int seconds)
{
  return (days(seconds) + 4) % 7;
}

String WeatherDay::report()
{
  return day(fcDate) + " " + TwoDigits(tempMax) + ".." + TwoDigits(tempMin) + " " + ThreeChars(windDirection) + TwoDigits(windSpeed) + " " + TwoDigits(precip) + "% " + weather + "\n";
};

String Weather::weatherReport()
{
  String w = location + "\n";
  for (int i = 0; i < WEATHER_DAYS; i++)
  {
    if (forecast[i].fcDate.length() > 0)
    {
      w += forecast[i].report();
    }
  }
  return w;
}

bool Weather::parseWeather(String &msg, unsigned long timestamp)
{
  Weather newWeather;
  if (newWeather.tparseWeather(msg, timestamp))
  {
    (*this) = newWeather;
    return true;
  }
  return false;
}
/* Met Office weather API*/
/*
bool Weather::tparseWeather(String& msg, unsigned long timestamp)
{
  //clogn("Parse weather ");
  location = "";
  forecastTimestamp = 0; // flag "invalid"
  for (int i = 0; i < WEATHER_DAYS; i++) {
    forecast[i].fcDate = "";
  }

  int msgix = 0;
  const String dateProlog = R"("type":"Day","value":")";

  msgix = msg.indexOf("\"Location\":");
  location = getProp(msg, "name", msgix, msgix + 10000);

  if (location == "") return false;

  //clogn(location);

  String report = location + "\n";

  bool gotLines = false;

  for (int i = 0; i < WEATHER_DAYS; i++) {
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
    if (cw.tempMin.length() == 0 || cw.tempMax.length() == 0) return false;
    report += cw.report();
  }
  clogn(report + "===");
  if (gotLines) {
    forecastTimestamp = timestamp;
  }
  return gotLines;
}
*/

HourlyArrayGetter::HourlyArrayGetter(arduino::String &msg, int msgix, arduino::String name)
{
  str = msg;
  if (msgix < 0)
    return;
  int nix = str.indexOf("\"" + name + "\":", msgix);
  if (nix < 0)
    return;
  int ix = str.indexOf("[", nix) + 1;
  endix = str.indexOf("]", ix);
}
arduino::String HourlyArrayGetter::next()
{
  int i = str.indexOf("\"", ix) + 1;
  int j = str.indexOf("\"", i);
  if (i < 0 || j < 0 || j > endix)
    return "";
  ix = j;
  return str.substring(i, j);
}
void HourlyArrayGetter::moveOn(int n)
{
  for (int i = 0; i < n; i++)
  {
    if (next().length() == 0)
      break;
  }
}
double HourlyArrayGetter::nextAverageOf(int n)
{
  double sum = 0;
  int count = 0;
  for (int i = 0; i < n; i++)
  {
    String vs = next();
    if (vs.length() == 0)
      break;
    count++;
    double v = vs.toDouble();
    sum += v;
  }
  if (count == 0)
    return 0;
  return sum / count;
}
double HourlyArrayGetter::nextMaxOf(int n)
{
  double max = -10000;
  for (int i = 0; i < n; i++)
  {
    String vs = next();
    if (vs.length() == 0)
      break;
    double v = vs.toDouble();
    if (v > max)
      max = v;
  }
  return max;
}
double HourlyArrayGetter::nextMinOf(int n)
{
  double min = 10000;
  for (int i = 0; i < n; i++)
  {
    String vs = next();
    if (vs.length() == 0)
      break;
    double v = vs.toDouble();
    if (v < min)
      min = v;
  }
  return min;
}

double HourlyArrayGetter::dominant360(int n)
{
  double *d = new double[n];
  double *p = new double[n];
  int count = 0;
  for (int i = 0; i < n; i++)
  {
    String vs = next();
    if (vs.length() == 0)
      break;
    count++;
    d[i] = vs.toDouble();
  }
  int minpc = 100000;
  int minpcix = 0;
  for (int i = 0; i < count; i++)
  {
    int pc = 0;
    for (int j = 0; j < count; j++)
    {
      double diff = abs(d[i]-d[j]);
      pc += diff>180 ? 360-diff : diff;
    }
    if (pc < minpc)
    {
      minpc = pc;
      minpcix = i;
    }
  }
  return d[minpcix];
}

/* open-meteo.com */

bool Weather::tparseWeather(String &msg, unsigned long timestamp)
{
  // clogn("Parse weather ");
  location = "52.06,-4.75";
  forecastTimestamp = 0; // flag "invalid"
  for (int i = 0; i < WEATHER_DAYS; i++)
  {
    forecast[i].fcDate = "";
  }

  int msgix = 0;
  const String dateProlog = R"("dt")";

  msgix = msg.indexOf("\"hourly\":");

  HourlyArrayGetter date(msg, msgix, "time");
  HourlyArrayGetter temp(msg, msgix, "temperature_2m");
  HourlyArrayGetter windSpeed(msg, msgix, "wind_speed_10m");
  HourlyArrayGetter windDirection(msg, msgix, "wind_direction_10m");
  HourlyArrayGetter precipitation(msg, msgix, "precipitation_probability");

  // Move each cursor to first 6am
  for (String s = date.next(); s.length() == 0 || s.indexOf("T06:") < 0; s = date.next())
  {
    temp.next();
    windSpeed.next();
    windDirection.next();
    precipitation.next();
    s = date.next();
  }

  String report = location + "\n";
  bool gotLines = false;

  for (int i = 0; i < WEATHER_DAYS; i++)
  {
    WeatherDay &cw = forecast[i];
    arduino::String dt = date.next();
    if (dt.length() == 0)
      break;
    date.moveOn(24);
    gotLines = true;
    cw.fcDate = dt.substring(0, 10);
    cw.tempMax = String("") + temp.nextMaxOf(12);
    cw.tempMin = String("") + temp.nextMinOf(12);
    cw.windSpeed = String("") + windSpeed.nextAverageOf(24);
    cw.windDirection = String("") + windDirection.dominant360(24);
    cw.precip = String("") + precipitation.nextAverageOf(12);
    cw.precipN = String("") + precipitation.nextAverageOf(12);
    report += cw.report();
  }

  clogn(report + "===");
  if (gotLines)
  {
    forecastTimestamp = timestamp;
  }
  return gotLines;
}

float Weather::getForecastTempDiff(float targetTemp)
{
  const float invAvgFactor = 1.0 - avgFactor;
  float avgTempDiff = 200;
  // int isAfternoon = rtc.getHours() > 12 ? 1 : 0;
  for (int i = WEATHER_DAYS - 1; i >= 0; i--)
  {
    if (forecast[i].fcDate.length() > 0)
    {
      float mint = forecast[i].tempMin.toFloat();
      float maxt = forecast[i].tempMax.toFloat();
      // clog (String("") + mint + ".." + maxt + " |");
      float diff = targetTemp - (forecast[i].tempMin.toFloat() * 2 + forecast[i].tempMax.toFloat()) / 3;
      if (avgTempDiff > 100)
        avgTempDiff = diff;
      else
        avgTempDiff = avgTempDiff * invAvgFactor + diff * avgFactor;
    }
  }
  float adjustedDeficit = avgTempDiff;
  int windSpeed = forecast[0].windSpeed.toInt();
  if (windSpeed > 0 && windSpeed < 100)
  {
    adjustedDeficit *= 1.0 + windSpeed * windSpeedFactor / 100;
  }
  dlogn(String("Deficit: ") + avgTempDiff + " Wind: " + windSpeed + " Adjusted deficit: " + adjustedDeficit);
  File f = SD.open("DEFICIT.TXT", FILE_REWRITE);
  if (f)
  {
    f.println(adjustedDeficit);
    f.close();
  }
  return adjustedDeficit;
}

String Weather::codes[30] =
    {"stars", "sun", "cloud", "cloud", "", "mist", "fog", "Cloud", "ocast", "shwr", "shwr", "drizl",
     "rain", "Shwr", "Shwr", "RAIN",
     "sleet", "sleet", "Sleet", "hail", "hail",
     "snow", "snow", "snow", "Snow", "Snow", "SNOW",
     "thndr", "thndr", "Thndr"};

/**** Tidal ********/

String Tidal::tidesReport()
{
  String dst = isSummertime() ? "BST" : "GMT";
  String r;
  for (int i = 0; i < 4; i++)
  {
    Tide &t = tides[i];
    if (t.eventType.length() > 0)
    {
      r += String(String(t.eventType).substring(0, 2) + " " + d2(t.day) + " " + d2(int(t.tod)) + ":" + d2(int(t.tod * 60) % 60) + " " + dst + " " + t.height + "\n");
    }
  }
  return r;
}

bool SunMoonResponseHandler::extractRiseSet(String s, String id, float &hour)
{
  int bodyStart = s.indexOf(id);
  if (bodyStart < 0)
    return false;
  int tStart = s.indexOf("T", bodyStart);
  String hourString = s.substring(tStart + 1, tStart + 3);
  String minuteString = s.substring(tStart + 4, tStart + 6);
  hour = hourString.toInt() + (0.0 + minuteString.toInt()) / 60.0;
  clogn(String(id) + " " + hour);
  return true;
}
void SunMoonResponseHandler::checkDone()
{
  if (tidal->sunSet > 0 && tidal->sunRise > 0 && tidal->moonRise >= 0 && tidal->moonSet >= 0)
  {
    (*doneSunMoon)(true);
  }
}

void MoonResponseHandler::gotResponse(int status, String content)
{
  if (extractRiseSet(content, "moonrise", tidal->moonRise) && extractRiseSet(content, "moonset", tidal->moonSet))
  {
    if (tidal->moonRise > tidal->moonSet)
      tidal->moonSet += 1.1; // tomorrow
    checkDone();
  }
}
void SunResponseHandler::gotResponse(int status, String content)
{
  if (extractRiseSet(content, "sunrise", tidal->sunRise) && extractRiseSet(content, "sunset", tidal->sunSet))
  {
    checkDone();
  }
}

MoonResponseHandler moonResponseHandler(&tidal);
SunResponseHandler sunResponseHandler(&tidal);

bool Tidal::getSunMoonAsync(SunMoonResponseHandler *responder, void (*done)(bool success))
{
  responder->setDoneHandler(done);
  String s;
  String req = "/weatherapi/sunrise/3.0/{3}?lat=52.07&lon=-4.75&date=20{0}-{1}-{2}&offset=+00:00";
  req.replace("{0}", d2(rtc.getYear()));
  req.replace("{1}", d2(rtc.getMonth()));
  req.replace("{2}", d2(rtc.getDay()));
  req.replace("{3}", responder->id());
  return getWebAsync((char *)"api.met.no", 443, req, "", responder, 100);
}

bool Tidal::getSunMoonAsync(void (*done)(bool success))
{
  moonRise = sunRise = moonSet = sunSet = -1;
  getSunMoonAsync(&sunResponseHandler, done);
  return getSunMoonAsync(&moonResponseHandler, done);
}

void Tidal::gotResponse(int status, String content)
{
  if (parseTides(content))
  {
    (*done)(true);
  }
}

bool Tidal::getTidesAsync(void (*doneHandler)(bool success))
{
  done = doneHandler;
  for (int i = 0; i < 4; i++)
    tides[i].eventType = "";
  const String tideReq = String("/Home/GetPredictionData?stationId=0490");
  return getWebAsync((char *)"easytide.admiralty.co.uk", 443, tideReq, "", this, 8);
}

bool Tidal::parseTides(String &msg)
{
  int mix = 0;
  bool current = false; // skip tides earlier than the most recent
  int cday = rtc.getDay();
  int chour = rtc.getHours();
  int tix = 0;
  bool summer = isSummertime();
  while (tix < 4)
  {
    Tide &t = tides[tix];
    int endSegment = msg.indexOf("}", mix);
    if (endSegment < 0)
    {
      clogn("endsegment");
      break;
    }
    t.eventType = getProp(msg, "eventType", mix, endSegment);
    String date = getProp(msg, "dateTime", mix, endSegment);
    // clog(date + "  ");
    t.height = getProp(msg, "height", mix, endSegment).toFloat();
    mix = endSegment + 1;
    if (date.length() < 16)
    {
      clogn(date + "-no date");
      continue;
    }
    t.day = date.substring(8, 10).toInt();
    t.tod = date.substring(11, 13).toFloat() + date.substring(14, 16).toFloat() / 60 + (summer ? 1.0 : 0);
    if (t.tod >= 24.0)
    {
      t.tod -= 24.0;
      t.day++;
    }
    // clogn(date + ">" + t.day + "T" + t.tod);
    if (t.day == cday && t.tod > chour - 6 || t.day > cday)
      current = true; // start collecting
    if (current)
      tix++;
  }
  if (logging)
  {
    String report = "";
    for (int tix = 0; tix < 4; tix++)
    {
      report += tides[tix].eventType + " " + tides[tix].day + "T" + tides[tix].tod + " " + tides[tix].height + "\n";
    }
    clogn(report);
  }
  return tix >= 2;
}
