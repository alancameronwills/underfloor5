/*

   TODO:
   - If can't get weather, should use recent weather to recalc heating plan from latest target temp.
   - Can't get weather should not cause embargo if the failed connection was completed.
  - Modularize
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
#include <Arduino_MKRENV.h> // https://docs.arduino.cc/hardware/mkr-env-shield 
#include <Sodaq_wdt.h>  // watchdog
#include <utility/wifi_drv.h> // for indicator lamp WiFiDrv

// Parameter default values, overridden by setup read from P file

bool logging = true;
int webClientTimeout = 12000;  // ms
int updateHour = 3; // AM and PM weather update
float targetTemp = 20;
float lowHoursPerDay = 0.5;  // Hours per day to run when away, to avoid condensation
float avgFactor = 0.7; // skew across forecast days: raise -> today has more influence
float windSpeedFactor = 0.1; // temp deficit multiplier per 100 mph
float minsPerDegreePerHour = 0.8;
unsigned long connectFailRebootTimeout = 2 * 60 * 60 * 1000L; // 2 hours
float hourlyWeights[24] =
{ 0, 0, 0, 0, 0, 0,
  1, 1, 1, 1, 0.5, 0.5,
  0.5, 0.5, 1, 1, 1, 1,
  1, 1, 1, 0, 0, 0
};


#define WIFI_SSID_COUNT 2
String wifiSSID[] = {"Pant-y-Wylan", "Pant-2-Wylan"};
int wifiSelected = 0;

// Onboard clock
RTCZero rtc;

Tidal tidal;
Weather weather;
Heating heating;
Temperatures temperatures(heating);
WebService webService;

void adjustTargetTemp(float t);
Screen screen (adjustTargetTemp);
Backlight backlight;


void truncateLog(const String& logfile = "LOG.TXT");


bool gotWeather;              // Weather got successfully, don't retry
bool gotTides;
bool gotSun;
bool periodsValid = false;    // Periods have been set since reboot
bool truncatedLog;            // Done the truncation this 12-hour
bool clocked = false;         // Done the 12-hourly update, don't do again this hour
float avgDeficit = -200.0;    // Average outside temp - target. Invalid <100.
// String remoteAddress;         // External address of house network from ping
bool isProtoBoard = false;    // False -> this is the real device; don't set web name.



// ENTRY: Initialization
void setup() {

  // Disable watchdog - may still be running after reset:
  sodaq_wdt_disable();

  
  pinMode(6, OUTPUT); // LED
  digitalWrite(6, LOW);
  WiFiDrv::pinMode(25, OUTPUT); //onboard LED green
  WiFiDrv::pinMode(26, OUTPUT); //onboard LED red
  WiFiDrv::pinMode(27, OUTPUT); //onboard LED blue

  gotWeather = false;
  clocked = false;
  periodsValid = false;

  rtc.begin();
  
  pinMode(SD_CS, OUTPUT);       // SD card chip select
  digitalWrite(SD_CS, HIGH);
  int sdOK = SD.begin(SD_CS);

  backlight.setup();
  heating.setup();
  screen.start();

  ENV.begin(); // https://docs.arduino.cc/hardware/mkr-env-shield

  // Are we running the real thing or the prototype?
  byte mac[6];
  WiFi.macAddress(mac);
  isProtoBoard = mac[0] != 0x50;
  wifiSelected = 0;

  if (logging) {
    Serial.begin(115200);
    int count = 0;
    while (!Serial) {
      // wait for serial port to connect.
      delay (100);
      if (count++ > 20) {
        logging = false;
        break;
      }
    }
  } else {
    delay(500);
  }

  dlogn(String("=====Restart=====") + (isProtoBoard ? String(" Proto ") + mac[0] : ""));
  if (!sdOK) dlogn("No SD card");

  transferRecentLog();
  getParams();
  sodaq_wdt_enable(WDT_PERIOD_8X);
}

unsigned long schedMinute = 0;

// ENTRY: run every 100ms
void loop() {
  TS_Point touch;

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
  webService.loop();

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

void tryConnections() {
  if (!gotWeather || rtc.getYear() < 18) {
    gotWeather = tryGetWeather();
    if (gotWeather)  {
      failCount = 0;
    }
    else {
      if (++failCount > 12) softReboot(); // sustained failures
    }
  }

  if (gotWeather) {
    if (gotTides && gotSun) pingConx();
    if (!gotTides) gotTides = tidal.getTides();
    if (!gotSun) gotSun = tidal.getSunMoon();
  }

  if (!truncatedLog) {
    truncateLog();
    truncateLog("TEMPERATURES.TXT");
    truncatedLog = true;
  }
}

void getAgain() {
  gotWeather = gotTides = gotSun = truncatedLog = false;
}

bool tryGetWeather() {
  bool success = false;
  backlight.on(true);
  showStatus("Connecting...");
  if (connectWiFi())
  {
    dlogn(ipString("IP "));
    showStatus(ipString("IP "));
    setTimeFromWiFi();
    if (weather.getWeather())
    {
      clogn("Got weather");
      success = true;
    }
    else {
      dlogn("Couldn't get weather");
      showStatus("Web fail weather");
    }
  }
  else {
    showStatus("No WiFi");
    dlogn("No WiFi");
  }
  if (setPeriodsFromWeather())
    periodsValid = true;
  if (!periodsValid)
  {
    dlogn("No weather - using default heating");
    setPeriodsFromDate();
    periodsValid = true;
  }
  clogn(heating.shortPeriodsReport());
  String vac = heating.lowUntilDate.length() > 0 ? String(" Vacation: ") + heating.lowUntilDate : String("");
  dlogn(String("Target ") + targetTemp + " Avg deficit: " + avgDeficit + vac + " Total heating: " + heating.totalHours);
  screen.switchToMainPage();
  return success;
}


void doItNow () {
  schedMinute = 0;    // do it now
  backlight.on(true);      // light up screen
  gotWeather = false; // force recalc with new parameters
}




void updateParameters(String& content) {
  clogn (String("UPDATE: ") + content);
  getFProp(content, "target", 4.0, 40.0, targetTemp);
  String lud = getProp(content, "vacation", 1, 10000);
  lud.trim();
  if (lud.length() == 10) lud = lud.substring(2); // want two-digit year
  if (lud.length() == 0) heating.lowUntilDate = "";
  else if (lud.length() == 8 && lud.charAt(2) == '-' && lud.charAt(5) == '-') {
    heating.lowUntilDate = lud;
    //checkLowUntil(); // cancel if already past // don't save params while open for reading!
  }
  getFProp(content, "factor", 0.01, 10.0, minsPerDegreePerHour);
  getFProp(content, "skew", 0.01, 1.0, avgFactor);
  getFProp(content, "lowHoursPerDay", 0.1, 4.0, lowHoursPerDay);
  getFProp(content, "windSpeedFactor", 0.1, 1.0, windSpeedFactor);
  float udh = -1.0;
  getFProp(content, "updateHour", 0, 23.99, udh);
  if (udh >= 0.0) updateHour = (int)udh;
  const String profileString ("\"profile\":[");
  int ix = content.indexOf(profileString);
  if (ix >= 0) {
    ix += profileString.length();
    for (int i = 0; i < 24; i++) {
      float w = content.substring(ix).toFloat();
      if (w >= 0.0 && w < 10)
        hourlyWeights[i] = w;
      ix = content.indexOf(",", ix) + 1;
      if (ix <= 0) break;
    }
  }
}

void getFProp(String &msg, String propName, float minf, float maxf, float & propVar) {
  String p = getProp(msg, propName, 1, 10000);
  if (p.length() > 0) {
    float t = p.toFloat();
    if (t >= minf && t <= maxf) propVar = t;
  }
}


// Save persistent data to SD card
void saveParams() {
  File f = SD.open("P.TXT", FILE_REWRITE);
  if (f) {
    String s;
    outParams(s);
    f.print(s);
    f.close();
    dlogn("Saved parameters");
  }
  else dlogn("Couldn't write parameters");
}

// Get persistent data from SD card
void getParams() {
  static char buf[1000];
  File f = SD.open("P.TXT", FILE_READ);
  if (f) {
    f.read(buf, 1000);
    String bb(buf);
    dlogn(String("Parameters from file:") + bb);
    updateParameters(bb);
    f.close();
  } else {
    dlogn(String("No saved parameters - using defaults"));
  }
}


void outParams (String&msg) {
  msg += outParams();
}

String outParams() {
  String t = String("{'target':%0,'vacation':'%1','factor':%2,'skew':%3,'updateHour':%4,'lowHoursPerDay':%5,'windSpeedFactor':%6,'profile':[%7]}");
  t.replace('\'', '"');
  t.replace("%0", String(targetTemp, 1));
  t.replace("%1", heating.lowUntilDate);
  t.replace("%2", String(minsPerDegreePerHour, 2));
  t.replace("%3", String(avgFactor, 2));
  t.replace("%4", String(updateHour));
  t.replace("%5", String(lowHoursPerDay));
  t.replace("%6", String(windSpeedFactor));
  t.replace("%7", outWeights());
  return t;
}

String outWeights() {
  String weights = "";
  for (int i = 0; i < 24; i++) {
    if (i % 6 == 0) {
      weights += "\n";
    }
    weights += String(hourlyWeights[i], 2) + (i < 23 ? "," : "");
  }
  return weights;
}

// Remind router we're here
void pingConx() {
  /*
    String reply;
    getWeb((char *)"cameronwills.org", 80, String("/cgi-bin/env.pl"), "", reply);
    int p = reply.indexOf("REMOTE_ADDR")+15;
    if (p > 15) {
      int p2 = reply.indexOf("<", p);
      if (p2 > p && p2-p < 16) {
        remoteAddress = reply.substring(p, p2);
      }
    }
  */
  if (connectWiFi()) {
    clog("ping ");
    unsigned long m = millis();
    WiFi.ping("google.co.uk");
    clogn(String(millis() - m));
  }
}

String ipString(const char *c) {
  IPAddress ip = WiFi.localIP();
  return String(c) + ip[0] + "." + ip[1] + "." + ip[2] + "." + ip[3];
}


bool sendWebReq (WiFiClient &client, char* host, int port, String request, String extraLine) {
  if (WiFi.status() != WL_CONNECTED) {
    if (!connectWiFi()) return false;
  }

  String req = String("GET ") + request + " HTTP/1.1\r\n"
               + extraLine
               + "Host: " + host + "\r\n"
               + "Accept: application/json charset=UTF-8\r\n"
               + "User-Agent: WeatherApp\r\n"
               + "Connection: close\r\n"
               + "Content-length: 0\r\n"
               + "\r\n";

  clogn(String("WEB REQ ") + host + ":" + port + "\n" + req);
  sodaq_wdt_reset();
  if (port == 443 ? client.connectSSL(host, port) : client.connect(host, port))
  {
    //clog(String(millis() % 100000) + "+");
    client.print(req);
    clogn("  SENT");
    return true;
  }
  else {
    clogn("Failed to connect");
    return false;
  }
}

bool getWebBlock (char* host, int port, String request, String extraLine, String& response, WiFiClient &client)
{
  clog(":");
  webIndicator(true);
  if (!sendWebReq(client, host, port, request, extraLine)) return false;

  response = "";

  int contentMax = 10000;
  int contentLength = contentMax; // Read from header
  int headerEnd = 0;     // Position of blank line
  const int bufsize = 3000;
  char buf [bufsize];
  unsigned long startTime = millis();
  bool timedOut = false;
  bool lengthGot = false;
  while (client.connected() && !timedOut && !lengthGot)
  {
    clog(String(millis() % 10000) + ":");
    while (client.available() && !timedOut && !lengthGot) {
      clog(String(millis() % 10000) + ".");
      int charCount = client.read((unsigned char*)buf, contentLength > 0 ? min(contentLength, bufsize - 1) : bufsize - 1);
      *(buf + charCount) = '\0'; // String terminator
      response += buf;
      if (contentLength == contentMax) {
        int icl = response.indexOf("Content-Length:");
        if (icl >= 0) {
          contentLength = response.substring(icl + 16, icl + 24).toInt();
          clogn(String("Content-Length: ") + contentLength);
        }
      }
      if (headerEnd == 0) {
        int eol = response.indexOf("\n");
        clogn(response.substring(0, eol));
      }
      if (headerEnd <= 0) headerEnd = response.lastIndexOf("\n\r\n") + 3;
      if (headerEnd <= 0) headerEnd = response.lastIndexOf("\n\n") + 2;

      timedOut = (unsigned long)(millis() - startTime) > webClientTimeout;
      lengthGot = headerEnd > 0 && contentLength > 0 && response.length() >= headerEnd + contentLength;
    }
    delay(10);
    timedOut = (unsigned long)(millis() - startTime) > webClientTimeout;
  }
  clogn(String("Get took: ") + ((unsigned long)(millis() - startTime) / 1000.0));
  client.stop();
  webIndicator(false);
  clogn(response);
  return response.length() > 200;
}



bool getWeb(char* host, int port, String request, String extraLine, String& response) {
  WiFiClient client;
  return getWeb(host, port, request, extraLine, response, client);
}

bool getWeb(char* host, int port, String request, String extraLine, String& response, WiFiClient &client)
{
  bool detail = false;
  bool tides = String(host).indexOf("easytide") >= 0;
  webIndicator(true);
  int charCount = 0; // including CRs
  int braceCount = 0;
  int headerEnd = 0; // start of body
  const int contentLengthMax = 20000;
  int contentLength = contentLengthMax; // read from Content-Length field
  bool lengthGot = false; // read contentLength chars from body
  bool timedOut = false;
  unsigned long startTime = millis();
  //clog(String(startTime % 100000) + ";");

  if (!sendWebReq(client, host, port, request, extraLine)) return false;
  response = "";
  clog(String("="));
  while (client.connected() && !timedOut && !lengthGot)
  {
    //clog(String(millis() % 100000) + ":");
    while (client.available() && client.connected() && !timedOut && !lengthGot) {
      //if (detail) clog(String(millis() % 100000) + "[");
      char c = client.read();
      //if (detail) clog(String(c));
      timedOut = (unsigned long)(millis() - startTime) > webClientTimeout;
      charCount++;
      if (contentLength > 0 && charCount >= contentLength) lengthGot = true;
      if (c == '\r') continue; // omit CRs except for counting
      response += c;
      if (c == '\n') {
        if (contentLength == contentLengthMax && headerEnd == 0) {
          clog("-");
          String s = String(response);
          s.toLowerCase();
          int icl = s.indexOf("content-length:");
          if (icl >= 0) {
            clog("#");
            contentLength = s.substring(icl + 16, icl + 24).toInt();
            clogn(String("Content-Length: ") + contentLength);
          }
        } else {
          clog("_");
        }
        if (headerEnd == 0 && charCount > 2 && response.charAt(response.length() - 2) == '\n') { // EOH
          headerEnd = charCount;
          charCount = 0;
        }
      }
      if (tides && c == '}' && braceCount++ > 8) lengthGot = true;
    }
    //clog(String(millis() % 100000) + "]");
    timedOut = (unsigned long)(millis() - startTime) > webClientTimeout;
    delay(10);
  }
  clogn(String("Get took: ") + ((unsigned long)(millis() - startTime) / 1000.0) + (timedOut ? " timed out" : ""));
  client.stop();
  webIndicator(false);
  clogn(response);
  return response.length() > 200;
}

void webIndicator(bool onOrOff)
{
  digitalWrite(6, onOrOff ? HIGH : LOW);
  WiFiDrv::analogWrite(26, onOrOff ? 255 : 0);
}




/*
   Heating control

*/



// Have we set the heating low until a specified date?
bool checkLowUntil () {
  if (heating.lowUntilDate.length() > 0) {
    long ludY = heating.lowUntilDate.substring(0, 2).toInt();
    long ludM = heating.lowUntilDate.substring(3, 5).toInt();
    long ludD = heating.lowUntilDate.substring(6).toInt();
    long rtcYMD = rtc.getYear() * 10000 + rtc.getMonth() * 100 + rtc.getDay();
    if (rtcYMD >= ludY * 10000 + ludM * 100 + ludD)  {
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
    File f = SD.open("DEFICIT.TXT", FILE_READ);
    if (!f) return false;
    else {
      char buf [20];
      int count = f.read(buf, 19);
      if (count <= 0 || count > 20) return false;
      buf[count] = '\0';
      String s (buf);
      float loggedDeficit = s.toFloat();
      if (loggedDeficit == 0.0) return false;
      dlogn(String("Using logged deficit ") + loggedDeficit);
      avgDeficit = loggedDeficit;
    }
  }
  heating.setPeriods(avgDeficit);
  return true;
}

float statsMean[12] = {4.5, 4.5, 5.0, 7.0, 11.0, 14.0, 15.0, 15.0, 12.0, 10.0, 7.0, 5.0};
// Approximate fallback if no forecast.
void setPeriodsFromDate()
{
  float avgTempDiff = targetTemp - 6.0;
  if (rtc.getYear() > 10) avgTempDiff = targetTemp - statsMean[rtc.getMonth() - 1];
  heating.setPeriods(avgTempDiff);
}

/*
   WiFi

*/
unsigned long connectFailStart = 0;
bool connectWiFi ()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    if (!checkReconnect()) {
      return false;
    }
    clog("Connecting");
    if (!isProtoBoard) WiFi.setHostname("heating.local");
    for (int tryCount = 0; tryCount < 3 && WiFi.status() != WL_CONNECTED; tryCount++) {
      WiFi.begin(wifiSSID[wifiSelected].c_str(), "egg2hell");
      int count = 20;
      while (WiFi.status() != WL_CONNECTED && count-- > 0)
      {
        clog(".");
        delay(200);
        digitalWrite(6, HIGH);
        delay(800);
        digitalWrite(6, LOW);
        sodaq_wdt_reset();
      }
      if (WiFi.status() != WL_CONNECTED) {
        wifiSelected = (wifiSelected + 1) % WIFI_SSID_COUNT;
      }
    }

    if (WiFi.status() == WL_CONNECTED) {
      clogn(String(":) ") + wifiSSID[wifiSelected]);
      connectFailStart = 0;
      webService.start();
    }
    else {
      clogn(":(");
      digitalWrite(6, HIGH);
      if (connectFailStart == 0) connectFailStart = millis();
      else {
        if ((unsigned long) (millis() - connectFailStart) > connectFailRebootTimeout) softReboot();
      }
    }
  }
  return WiFi.status() == WL_CONNECTED;
}

/*
   Prevent repeated connection attempts resulting in restarts.

*/

const int embargoPeriod = 15; // minutes
int embargoUntil = -1; // ToD in minutes to resume attempts
bool checkReconnect() {
  if (logging || isProtoBoard) return true;
  bool goAhead = true;
  int nowMinutes = rtc.getHours() * 60 + rtc.getMinutes();
  if (embargoUntil >= 0) {
    // We've already read the previous reconnect timestamp, and decided to embargo reconnection attempts.
    // Don't attempt reconnect until the embargo has expired and the heating is off.
    if (heating.isHeatingOn) return false;
    // Assuming embargo period < 12h. 720m==12h, 1440m==24h
    int expired = (nowMinutes - embargoUntil + 720) % 1440 - 720;
    // Could be invalid if RTC was reset. Ignore unbelievably long embargo.
    if (expired < 0 && (0 - expired) <= embargoPeriod) return false;
    // Cancel embargo:
    embargoUntil = -1;
  }
  else {
    // No embargo currently set. Might be first attempt after reset.
    // Look to see if there was a recent reconnection attempt.
    const int bufsz = 40;
    char buf[bufsz];
    File f = SD.open("TRY_CONX.TXT", FILE_READ);
    if (f) {
      int count = f.read(buf, bufsz - 1);
      f.close();
      buf[count] = '\0';
      String s (buf);

      // If last connection (maybe before a reset) was recent, don't retry for a while.
      int loggedTodMinutes = s.toInt();
      // Ludicrous definition of % for -ve input.
      int sinceLastConnect = (nowMinutes - loggedTodMinutes + 1440) % 1440; // 24h
      if (sinceLastConnect < embargoPeriod)
      {
        // There was a recent reconnection attempt.
        // Embargo reconnections for a while.
        embargoUntil = (loggedTodMinutes + embargoPeriod) % 1440;
        goAhead = false;
        dlogn(String("Embargo reconnect until ") + d2((int)(embargoUntil / 60)) + ":" + embargoUntil % 60);
      }
      else {
        goAhead = true;
      }
    } else {
      goAhead = true;
    }
  }
  if (goAhead) {
    File ff = SD.open("TRY_CONX.TXT", FILE_REWRITE);
    ff.println(String(nowMinutes) + " #" + avgDeficit);
    ff.close();
  }
  return goAhead;
}

void setTimeFromWiFi()
{
  if (WiFi.status() != WL_CONNECTED) return;
  rtc.begin();
  unsigned long timeInSeconds = 0;
  for (int tries = 0; tries < 20; tries++)
  {
    timeInSeconds = WiFi.getTime();
    if (timeInSeconds > 0) break;
    delay(500);
    digitalWrite(6, HIGH);
    delay(500);
    digitalWrite(6, LOW);
    sodaq_wdt_reset();
  }
  if (timeInSeconds > 0) {
    rtc.setEpoch(timeInSeconds);
    if (isSummertime())
    {
      rtc.setEpoch(timeInSeconds + 3600);
    }
    clogn("Got time");
  }
  else dlogn ("Failed to get time");
}



// Let the watchdog time out
void softReboot() {
  dlogn("Reboot");
  sodaq_wdt_enable();
  while (true);
}
