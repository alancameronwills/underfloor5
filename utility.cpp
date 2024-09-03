#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "utility.h"
#include <RTCZero.h>    // clock

extern RTCZero rtc;

String dayNames [] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
String dayName(int i) {
  return dayNames[i];
}

String utc (long int seconds)
{
  time_t epoch = seconds;
  return asctime(gmtime(&epoch));
}

int months [] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};

int dayIndex(String date)
{
  int y = date.substring(2, 4).toInt();
  int m = date.substring(5, 7).toInt();
  int d = date.substring(8).toInt();
  return (y * 365 + months[m - 1] + d - 1 + (y * 12 + m - 2) / 48) % 7;
}

String day (String date)
{
  return date + " " + dayName(dayIndex(date));
}

bool isSummertime () {
  int month = rtc.getMonth();
  int year = rtc.getYear() + 2000;
  int day = rtc.getDay();
  if (month < 3 || month > 10) return false;
  if (month > 3 && month < 10) return true;
  if (month == 3) return day >= (31 - (((5 * year / 4) + 4) % 7));
  if (month == 10) return day < (31 - (((5 * year / 4) + 1) % 7));
}

long rtcSeconds() {
  return rtc.getY2kEpoch();
}

String getProp (String &msg, String prop, int msgix, int endSegmentIx)
{
  if (msgix < 0) return "";
  int ix = msg.indexOf("\"" + prop + "\":", msgix);
  if (ix < 0 || ix > endSegmentIx) return "";
  int startPropIx = ix + prop.length() + 3;
  if (msg[startPropIx] == '"') startPropIx++;
  int endPropIx = msg.indexOf('"', startPropIx);
  return msg.substring(startPropIx, endPropIx);
}
