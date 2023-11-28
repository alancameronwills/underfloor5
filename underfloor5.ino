/*

   TODO:
   - If can't get weather, should use recent weather to recalc heating plan from latest target temp.
   - Can't get weather should not cause embargo if the failed connection was completed.
  - non-blocking file operations
  - checkLowUntil should schedule saveParams for next cycle, not do it immediately

  Control underfloor heating.

  !!!! Before compiling, set correct device type under Tools > Board:

  ARDUINO MKR WiFi 1010

  SPI pins:

  A0 - onboard LED
  A1 -
  A2 - BACKLIGHT  - TFT LED backlight
  A3 - TFT_CS   - Display chip select
  A4 - TOUCH_CS - Touch device chip select
  A5 - LDR - sensor for backlight switch
  A6 - THERMISTOR INPUT

  0 - TFT_DC
  1 - TOUCH_IRQ
  2 - BEEPER
  3 -
  4 - SD_CS - SD card chip select
  5 -
  6 - ON-BOARD LED
  7 - HEATING POWER RELAY
  8 - TFT_MOSI
  9 - TFT_CLK
  10- TFT_MISO
  11- SPI bus - SD card
  12- SPI bus
  13- SPI bus


  NB Security certs must be preloaded for each SSL site we use.
  https://support.arduino.cc/hc/en-us/articles/360016119219-How-to-add-certificates-to-Wifi-Nina-Wifi-101-Modules-

  UX:
  - Wave to get backlight - weather charts; current and target temp; time; IP addr; buttons:  BOOST | OFF | HOLS
  - Touch temp => up/down buttons
  - Touch graph => Graph of recent temperature and heating; heating plan block chart;

*/


#include <Arduino.h>
#include <SPI.h>
#include <SD.h>         // SD card for log and param files
#include <WiFiNINA.h>   // on-board wifi
#include <WiFiUdp.h>
#include <RTCZero.h>    // clock
#include "utility.h"
#include "logger.h"
#include "outside.h"    // Tidal, Weather
#include "inside.h"     // Temperatures, Heating
#include "screen.h"
#include "webservice.h"
#include "webclient.h"
#include "parameters.h"
#include <Sodaq_wdt.h>  // watchdog


bool debugging = true;

bool logging = true;

// Onboard clock
RTCZero rtc;

Tidal tidal;
Weather weather;
Heating heating;
Temperatures temperatures;
WebService webservice;

void adjustTargetTemp(float t);
Screen screen(adjustTargetTemp);
Backlight backlight;


void truncateLog(const String& logfile = "LOG.TXT");


bool gotWeather;              // Weather got successfully, don't retry
bool gotTides;
bool gotSun;
bool periodsValid = false;    // Periods have been set since reboot
bool truncatedLog;            // Done the truncation this 12-hour
bool clocked = false;         // Done the 12-hourly update, don't do again this hour
float avgDeficit = -200.0;    // Average outside temp - target. Invalid <100.


// ENTRY: Initialization
void setup() {

  // Disable watchdog - may still be running after reset:
  sodaq_wdt_disable();
  sodaq_wdt_enable(WDT_PERIOD_8X);

  gotWeather = false;
  clocked = false;
  periodsValid = false;

  rtc.begin();
  sd_logger_start();
  backlight.setup();
  heating.setup();
  screen.start();
  webservice.start();

  transferRecentLog();
  getParams();
}

unsigned long schedMinute = 0;

// ENTRY: run every 100ms
void loop() {
  clearRecentLog();

  // Once a minute
  unsigned long m = millis();        // timer
  if ((long)(m - schedMinute) > 0) {
    minuteTasks();
    schedMinute = 60000 + m;
  }

  // Respond to photocell
  backlight.loop(m); // light up screen if rqd

  // Serve incoming web request:
  webservice.loop();

  // check for responses to outgoing web requests:
  webClientLoop();

  screen.loop();

  delay(100);
  sodaq_wdt_reset(); // watchdog
  // The watchdog resets the board if we don't reset it
  // within its timeout period. Guards against hang-ups.
}

/** 0..255 colours to TFT colour
*/

int failCount = 0;
int minuteCount = 0;
void minuteTasks() {
  digitalWrite(6, HIGH); // Light the LED

  if (minuteCount == 0 || minuteCount == 1) tryConnections();
  minuteCount = (minuteCount + 1) % 10;

  if (rtc.getHours() == updateHour || rtc.getHours() == updateHour + 12) {
    if (!clocked) {
      getAgain();
      clocked = true;
    }
  }
  else {
    clocked = false;
  }

  heating.switchHeating(); // switch on or off
  screen.refresh();
  temperatures.record();
  digitalWrite(6, LOW);
}

void getSunMoonDone(bool ok) {
  gotSun = ok;
  dlogn(String("Sun ") + tidal.sunRise + ".." + tidal.sunSet + " Moon " + tidal.moonRise + ".." + tidal.moonSet);
  screen.refresh();
}

void getTidesDone(bool ok) {
  gotTides = ok;
  dlogn(tidal.tidesReport());
  screen.refresh();
}

void tryConnections() {
  if (!gotWeather || rtc.getYear() < 18) {
    gotWeather = tryGetWeather();
    if (gotWeather) {
      failCount = 0;
    }
    else {
      if (++failCount > 12) softReboot(); // sustained failures
    }
  }

  if (gotWeather) {
    if (gotTides && gotSun) pingConx();
    if (!gotTides) tidal.getTidesAsync(getTidesDone);
    if (!gotSun) tidal.getSunMoonAsync(getSunMoonDone);
  }

  if (!truncatedLog) {
    truncateLog();
    truncateLog("TEMPERAT.TXT");
    truncatedLog = true;
  }
}

void getAgain() {
  gotWeather = gotTides = gotSun = truncatedLog = false;
}

void gotWeatherHandler(bool ok) {
  avgDeficit = weather.getForecastTempDiff(targetTemp);
  gotWeather = setPeriodsFromWeather();
  if (gotWeather) {
    clogn("Got weather");
    clogn(heating.shortPeriodsReport());
    String vac = heating.lowUntilDate.length() > 0 ? String(" Vacation: ") + heating.lowUntilDate : String("");
    dlogn(String("Target ") + targetTemp + " Avg deficit: " + avgDeficit + vac + " Total heating: " + heating.totalHours);
  } else ("Not got weather");
}

bool tryGetWeather() {
  bool success = false;
  backlight.on(true);
  showStatus("Connecting...");
  if (webservice.connectWiFi())
  {
    dlogn(webservice.ipString("IP "));
    showStatus(webservice.ipString("IP "));
    setTimeFromWiFi();
    weather.getWeather(gotWeatherHandler);
  }
  else {
    showStatus("No WiFi");
    dlogn("No WiFi");
  }
  setPeriodsFromDate();
  screen.switchToMainPage();
  return success;
}


void doItNow() {
  schedMinute = 0;    // do it now
  backlight.on(true);      // light up screen
  gotWeather = false; // force recalc with new parameters
}


/*
   Heating control

*/

// Have we set the heating low until a specified date?
bool checkLowUntil() {
  if (heating.lowUntilDate.length() > 0) {
    long ludY = heating.lowUntilDate.substring(0, 2).toInt();
    long ludM = heating.lowUntilDate.substring(3, 5).toInt();
    long ludD = heating.lowUntilDate.substring(6).toInt();
    long rtcYMD = rtc.getYear() * 10000 + rtc.getMonth() * 100 + rtc.getDay();
    if (rtcYMD >= ludY * 10000 + ludM * 100 + ludD) {
      heating.lowUntilDate = "";
      dlogn("Cancelled vacation");
      saveParams();
    }
  }
  return heating.lowUntilDate.length() > 0;
}


bool setPeriodsFromWeather()
{
  if (avgDeficit < -20 || avgDeficit > 40) {
    String s = getShortFileContent("DEFICIT.TXT");
    if (s.length() == 0) return false;
    float loggedDeficit = s.toFloat();
    if (loggedDeficit == 0.0) return false;
    dlogn(String("Using logged deficit ") + loggedDeficit);
    avgDeficit = loggedDeficit;
  }
  heating.setPeriods(avgDeficit);
  return true;
}

float statsMean[12] = { 4.5, 4.5, 5.0, 7.0, 11.0, 14.0, 15.0, 15.0, 12.0, 10.0, 7.0, 5.0 };
// Approximate fallback if no forecast.
void setPeriodsFromDate()
{
  float avgTempDiff = targetTemp - 6.0;
  if (rtc.getYear() > 10) avgTempDiff = targetTemp - statsMean[rtc.getMonth() - 1];
  heating.setPeriods(avgTempDiff);
}



// Let the watchdog time out
void softReboot() {
  dlogn("Reboot");
  sodaq_wdt_enable();
  while (true);
}
