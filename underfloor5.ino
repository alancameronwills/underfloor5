/*

   TODO:
   - If can't get weather, should use recent weather to recalc heating plan from latest target temp.
   - Can't get weather should not cause embargo if the failed connection was completed.
  - Modularize
  - non-blocking file operations

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
#include <Arduino_MKRENV.h> // https://docs.arduino.cc/hardware/mkr-env-shield 
#include <Sodaq_wdt.h>  // watchdog
#include <utility/wifi_drv.h> // for indicator lamp WiFiDrv

// Parameter default values, overridden by setup read from P file

bool logging = true;
int webClientTimeout = 12000;  // ms
int updateHour = 3; // AM and PM weather update
float targetTemp = 20;
String lowUntilDate = "";    // Use when on holiday YY-MM-DD
bool serviceOff = false;
long serviceOn = 0;
float lowHoursPerDay = 0.5;  // Hours per day to run when away, to avoid condensation
float avgFactor = 0.7; // skew across forecast days: raise -> today has more influence
float windSpeedFactor = 0.1; // temp deficit multiplier per 100 mph
float minsPerDegreePerHour = 0.8;
float insolationFactor = 0.4; // varies minsPerDegreePerHour from 0.4 to 1.2, winter to summer
float minimumBurst = 10.0; // minutes
float startupTime = 3.0; // minutes before heating actually starts
unsigned long backlightTimeout = 120 * 1000L; // ms
int backlightSensitivity = 12; // 3..20
unsigned long connectFailRebootTimeout = 2 * 60 * 60 * 1000L; // 2 hours
float hourlyWeights[24] =
{ 0, 0, 0, 0, 0, 0,
  1, 1, 1, 1, 0.5, 0.5,
  0.5, 0.5, 1, 1, 1, 1,
  1, 1, 1, 0, 0, 0
};
float cosMonth[12] = {1.0, 0.7, 0.25, -0.2, -0.7, -1.0, -1.0, -0.7, -0.2, 0.25, 0.7, 1.0};


#define WIFI_SSID_COUNT 2
String wifiSSID[] = {"Pant-y-Wylan", "Pant-2-Wylan"};
int wifiSelected = 0;



#define LDR_PIN A5


#define THERMISTOR A6

#define HEAT_PIN 7  // Heating power relay



// Onboard clock
RTCZero rtc;

Tidal tidal;
Weather weather;
Temperatures temperatures;
Heating heating;

void adjustTargetTemp(float t);

Screen screen (adjustTargetTemp);


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
bool isBacklightOn = true;      // Display backlight is lit, will time out

WiFiServer server(80);


// ENTRY: Initialization
void setup() {

  // Disable watchdog - may still be running after reset:
  sodaq_wdt_disable();

  gotWeather = false;
  clocked = false;
  periodsValid = false;

  pinMode(BACKLIGHT, OUTPUT);     // screen Backlight
  backlightOn(true);

  pinMode(SD_CS, OUTPUT);       // SD card chip select
  digitalWrite(SD_CS, HIGH);

  pinMode(TFT_CS, OUTPUT);    // screen chip select
  digitalWrite(TFT_CS, HIGH);

  pinMode(6, OUTPUT); // LED
  digitalWrite(6, LOW);

  heating.isHeatingOn = false;
  pinMode(HEAT_PIN, OUTPUT); // Heating relay
  digitalWrite(HEAT_PIN, LOW); // LOW == OFF


  WiFiDrv::pinMode(25, OUTPUT); //onboard LED green
  WiFiDrv::pinMode(26, OUTPUT); //onboard LED red
  WiFiDrv::pinMode(27, OUTPUT); //onboard LED blue


  rtc.begin();
  int sdOK = SD.begin(SD_CS);

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
  backlight(m); // light up screen if rqd

  // Serve incoming web request:
  WiFiClient sclient = server.available();
  if (sclient) {
    clogn("server client");
    serveClient(sclient);
  }

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
  backlightOn(true);
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
  String vac = lowUntilDate.length() > 0 ? String(" Vacation: ") + lowUntilDate : String("");
  dlogn(String("Target ") + targetTemp + " Avg deficit: " + avgDeficit + vac + " Total heating: " + heating.totalHours);
  screen.switchToMainPage();
  isBacklightOn = true;
  return success;
}


//  Backlight control

unsigned long backlightWentOn = 0;
int recentLux = 0;
int skip = 0;
int blinker = 0;
void backlight(unsigned long m) {
  skip = (skip++) % 3;
  if (skip != 0) return;
#ifdef LDR_PIN
  int lux = analogRead(LDR_PIN); // Light dependent resistor
  if (abs(recentLux - lux) > backlightSensitivity && blinker == 0)
  {
    clogn(String("BL ") + recentLux + "  " + lux);
    if (!isBacklightOn) backlightWentOn = m;
    else showIP();
  }
  recentLux = lux;
  if (blinker > 0) blinker--;
  if (((long)(m - backlightWentOn - backlightTimeout) > 0) == isBacklightOn) {
    isBacklightOn = !isBacklightOn;
    backlightOn(isBacklightOn);
    blinker = 2; // suppress response to own change
  }
#else
  if (!isBacklightOn) {
    isBacklightOn = true;
    backlightOn(true);
  }
#endif
}



// Files on SD card
void listDirectory(File dir, int indent, String&out) {
  while (true) {
    File entry =  dir.openNextFile();
    if (! entry) {
      break;
    }
    for (uint8_t i = 0; i < indent; i++) {
      out += '\t';
    }
    out += entry.name();
    if (entry.isDirectory()) {
      out += "/\n";
      listDirectory(entry, indent + 1, out);
    } else {
      // files have sizes, directories do not
      out += "\t\t";
      out += entry.size();
      out += "\n";
    }
  }
  dir.close();
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


// WiFi server
void serveClient(WiFiClient& client)
{
  String request;
  String content;
  getRequestFromClient(client, request, content);
  if (content.length() > 0 && (request.indexOf(" /upd ") > 0 || request.indexOf("POST / ") >= 0)) {
    respondParameterUpdate(client, request, content);
  }
  else if (request.indexOf("favicon.ico") > 0) respondIcon(client, request, content);
  else if (request.indexOf(" / ") > 0) respondT(client, request, content);
  else if (request.indexOf(" /service?") > 0) {
    setOffOrOn(request);
    respondT(client, request, content);
  }
  else respond(client, request, content);
  delay(1);
  client.flush();
  client.stop();
  clogn("Disconnected");
  if (request.indexOf(" /reboot") > 0) softReboot();
}

// Set on or off immediately for service purposes
void setOffOrOn (String request) {
  serviceOff = false;
  serviceOn = 0;
  if (request.indexOf("?set=off ") > 0) serviceOff = true;
  else if (request.indexOf("?set=on ") > 0) serviceOn = millis() + 30 * 60 * 1000;
  schedMinute = 0; // do it now
  recentLux = 0; // light up screen
}

void adjustTargetTemp(float t) {
  if (t > 8.0 && t < 30)
  {
    targetTemp = t;
    saveParams();       // persist to disc
    gotWeather = false; // force recalc with new parameters
    schedMinute = 0;    // do it now
  }
}

void respondParameterUpdate(WiFiClient& client, String &request, String &content) {
  updateParameters(content);
  saveParams();       // persist to disc
  gotWeather = false; // force recalc with new parameters
  schedMinute = 0;    // do it now
  recentLux = 0;      // light up screen

  //client.println("HTTP/1.1 303 See other");
  //client.println("Location: /");
  //client.println("");
  client.println("HTTP/1.1 200 OK");
  client.println("");
  client.println("<!DOCTYPE HTML>");
  client.println("<html><head><meta http-equiv=\"refresh\" content=\"10;URL='/'\"/></head><body>Updating...</body></html>");
}

bool saveTemplate(String &content) {
  int start = content.indexOf("=");
  if (start < 0) return false;
  File f = SD.open("TEMPLATE.HTM", FILE_REWRITE);
  if (!f) return false;
  f.write(content.c_str() + start + 1);
  f.close();
  return true;
}

void getRequestFromClient(WiFiClient& client, String &request, String &content) {
  boolean currentLineIsBlank = true;
  int contentLength = 0;
  bool readingContent = false;
  int timeout = 2000;
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      if (readingContent) {
        content += c;
        if (--contentLength <= 0) break;
      }
      else {
        request += c;
        // Blank line terminates header:
        if (c == '\n' && currentLineIsBlank) {
          // parse request
          int ix = request.indexOf("Content-Length:");
          if (ix >= 0) {
            contentLength = request.substring(ix + 15).toInt();
            clogn(String("content ") + contentLength);
          }
          if (contentLength == 0) break;
          else readingContent = true;
        }
        if (c == '\n') {
          // starting a new line
          currentLineIsBlank = true;
        }
        else if (c != '\r') {
          currentLineIsBlank = false;
        }
      }
    }
    else
    {
      delay(1);
      if (--timeout <= 0) {
        clogn("timeout");
        break;
      }
    }
  }
  while (client.available()) client.read();
  decode(content);
  clogn(request);
  clogn(content);
}


void updateParameters(String& content) {
  clogn (String("UPDATE: ") + content);
  getFProp(content, "target", 4.0, 40.0, targetTemp);
  String lud = getProp(content, "vacation", 1, 10000);
  lud.trim();
  if (lud.length() == 10) lud = lud.substring(2); // want two-digit year
  if (lud.length() == 0) lowUntilDate = "";
  else if (lud.length() == 8 && lud.charAt(2) == '-' && lud.charAt(5) == '-') {
    lowUntilDate = lud;
    checkLowUntil(); // cancel if already past
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

// Web page icon is a single block of colour. The three bytes last-8..6 are the icon colour. K==0, J==255.
char* icoCode = (char *)"KKLKLKLLKKLKcK{KKKaKKKsKKKLKKKMKKKLKcKKKKKKKKKKKKKKKKKKKKKKKKKzJzKKKKK";
byte icobytes [70];
void respondIcon (WiFiClient& client, String& req, String& content)
{
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: image/x-icon");
  client.println("Content-Length: 70");
  client.println("Connection: close");  // the connection will be closed after completion of the response
  client.println();
  for (int i = 0; i < 70; i++) {
    icobytes[i] = icoCode[i] - 75;
  }
  client.write(icobytes, 70);
  client.flush();
}

int unhex(char c) {
  return c < 'A' ? c - '0' : c - 'A' + 10;
}

void decode(String &m) {
  char b[m.length() + 1];
  int mi = 0;
  for (int i = 0; i < m.length(); ) {
    if (i < m.length() - 2 && m[i] == '%') {
      char c = (char)(unhex(m[i + 1]) * 16 + unhex(m[i + 2]));
      b[mi++] = c;
      i += 3;
    } else if (m[i] == '+' || (unsigned)m[i] < 32) {
      b[mi++] = ' ';
    } else {
      b[mi++] = m[i++];
    }
  }
  b[mi] = '\0';
  m = String(b);
  // clogn(String("DECODE: ") + m);
}

// Create a web page by substituting static var values into a template file.
// Current parameters are: {%temp} {%factor} {%vacation}
// Template file is template.htm
void respondT(WiFiClient& client, String& req, String& content) {
  const int maxParamNameLength = 20;
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();

  const int bufsize = 1000; // Max block size
  char buf [bufsize];
  int offset = 0; // Start point for reading next block. Nearly always 0.
  File templateFile = SD.open("TEMPLATE.HTM", FILE_READ);
  clog("Template");
  // Copy to client in blocks:
  while (templateFile.available()) {
    clog(":");
    // Read a block into the buffer:
    // Offset > 0 iff a chunk of parameter has been carried over from previous block.
    int length = templateFile.read(buf + offset, bufsize - 1 - offset);
    offset = 0;


    int ix = 0, // We've copied to output up to this point.
        pbx = 0,  // Start of a parameter: {%
        pex = 0;  // End of parameter: }
    while (ix < length) {
      clog(";");
      pbx = findParameter(buf, ix, length);
      if (pbx >= 0) {
        // Find the end of the parameter, or end of block, or give up as too long:
        for (pex = pbx + 1; buf[pex] != '}' && pex < length && pex < pbx + maxParamNameLength; pex++);
        if (buf[pex] != '}') {
          if (pex < length) {
            // Error: unterminated flag. Ignore parameter and continue as usual.
            client.write(buf + ix, pex - ix);
            ix = pex;
            clog("!");
          } else {
            // Parameter crosses buffer boundary. Process it in next block.
            // Write out buffer up to parameter:
            client.write(buf + ix, pbx - ix);
            ix = length; // End of block
            // Copy the fragment of the parameter to the start of the buffer:
            for (int tx = 0, fx = pbx; fx < length; tx++, fx++) buf[tx] = buf[fx];
            // Read in the next block after the copied fragment:
            offset = pex - pbx;
            clog("~");
          }
        }
        else {
          // Found parameter.
          // Copy up to start of parameter:
          client.write(buf + ix, pbx - ix);

          // Read parameter name and print its value:
          buf[pex] = '\0'; // End-of-string flag overwrites '}'
          String paramName ((char*)(buf + pbx + 2)); //
          clog (String("") + paramName);
          String value = paramName.equalsIgnoreCase("temp") ? String(targetTemp, 1) :
                         paramName.equalsIgnoreCase("factor") ? String(minsPerDegreePerHour) :
                         paramName.equalsIgnoreCase("vacation") ? lowUntilDate :
                         paramName.equalsIgnoreCase("serviceState") ? String(serviceOff ? "SERVICE OFF" : serviceOn > 0 ? "SERVICE ON" : "") :
                         paramName.equalsIgnoreCase("params") ? outParams() :
                         "????";
          client.print(value);
          clog(String("=") + value);

          // Continue processing from end of parameter:
          ix = pex + 1;
        }
      }
      else {
        // No further parameters. Copy rest of buffer:
        client.write(buf + ix, length - ix);
        ix = length; // End of block
      }
    }
  }
  templateFile.close();
  client.flush();
  clogn("#");
}

int findParameter(char *buf, int start, int end) {
  for (int ix = start; ix < end; ix++) {
    if (buf[ix] == '{' && buf[ix + 1] == '%') return ix;
  }
  return -1;
}

void logPage(WiFiClient& client, const String& logfile = "LOG.TXT") {
  char buf [1000];
  client.println("<a href='/'>Home</a><pre>");
  File f = SD.open(logfile);
  if (f) {
    while (f.available()) {
      size_t cc = f.read(buf, 1000);
      client.write(buf, cc);
    }
    f.close();
  }
  else client.println("Can't open log file");
  client.println("</pre><a href='/'>Home</a><br/><br/><a href='/delete'>Delete log</a><br/><br/><a href='/reboot'>Reboot</a>");
}

void deleteLogPage(WiFiClient& client, const String& logfile = "LOG.TXT") {
  File f = SD.open(logfile, FILE_REWRITE);
  if (f) {
    f.println("");
    f.close();
    client.println("<p>Deleted log page</p>");
  } else {
    client.println("<p>Couldn't delete log page</p>");
  }
  client.println("<a href='/'>Home</a><br/>");
}

void respond(WiFiClient& client, String& req, String& content) {
  // send a standard http response header
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");    // xxxx keep-alive?
  client.println();
  client.println("<!DOCTYPE HTML>");
  client.println("<html><body>");
  if (req.indexOf(" /log") > 0) {
    logPage(client);
  } if (req.indexOf(" /temps") > 0) {
    logPage(client, "TEMPERATURES.TXT");
  } else if (req.indexOf(" /delete") > 0) {
    deleteLogPage(client);
    deleteLogPage(client, "TEMPERATURES.TXT");
  } else if (req.indexOf(" /reboot") > 0) {
    client.println("<b>Rebooting</b>");
  } else if (req.indexOf("POST /template") >= 0) {
    if (saveTemplate(content)) client.println("Template saved");
    else client.println("Couldn't save template");
  } else if (req.indexOf("GET /template") >= 0) {
    client.println("<form method='POST'><textarea name='x'></textarea><input type='submit'/></form>");
  } else {
    statusPage(client, req.indexOf(" /upd") > 0); // GET or POST but not Referer
  }
  client.println("</body></html>");
}



void statusPage(WiFiClient& client, bool isUpd) {
  client.println("<pre>");
  String body = timeString() + "   " + (serviceOff ? "SERVICE OFF  " : serviceOn > 0 ? "SERVICE ON  " : "")
                + (heating.isHeatingOn ? "ON" : "OFF") + "\n\n\n"
                + "Weather:\n\n" + weather.weatherReport()
                + "\nAvg Deficit: " + String(avgDeficit, 2)
                + "\n\n\nTides:\n\n" + tidal.tidesReport() + "\n\n";
  body += heating.periodsReport();
  /*body += "\n\nParameters\n";
    outParams(body);*/
  body += "\n\nFiles\n";
  listDirectory(SD.open("/"), 0, body);
  //body += "\n\nRemote address: " + remoteAddress;

  for (int ix = 0; ix < body.length(); ix += 1000)
  {
    client.println(body.substring(ix, min(ix + 1000, body.length())));
  }
  client.println("</pre>");
  if (isUpd) {
    client.println("Parameters: <form action='upd' method='post'><textarea name='p' rows='8' cols='70'>");
    String outp; outParams(outp); client.println(outp);
    client.println("</textarea><input type='submit' value='Update'/></form>");
    client.println("<a href='/'>Close</a>");
  }
  else {
    client.println("<a href='/upd'>Change parameters</a>&nbsp;&nbsp;&nbsp;<a href='/log'>Log</a>");
  }
}

void outParams (String&msg) {
  msg += outParams();
}

String outParams() {
  String t = String("{'target':%0,'vacation':'%1','factor':%2,'skew':%3,'updateHour':%4,'lowHoursPerDay':%5,'windSpeedFactor':%6,'profile':[%7]}");
  t.replace('\'', '"');
  t.replace("%0", String(targetTemp, 1));
  t.replace("%1", lowUntilDate);
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

String Heating::periodsReport() {
  String r;
  totalHours = 0.0;
  r += "Periods: \n";
  for (int i = 0; i < 24; i++) {
    r += d2((int)period[i]) + " ";
    totalHours += (period[i] / 60);
    if (i % 6 == 5) r += "\n";
  }
  r += "\nTotal hours: ";
  r += totalHours;
  return r;
}

String Heating::shortPeriodsReport() {
  String r ("Heat: ");
  totalHours = 0.0;
  for (int i = 0; i < 24; i++) {
    r += (int)(period[i] / 6 + 0.5);
    totalHours += (period[i] / 60);
    if (i % 6 == 5) r += " ";
  }
  r += " Total hours: ";
  r += totalHours;
  return r;
}

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
  const char* location = "353070";
  const String weatherReq = String("/public/data/val/wxfcs/all/json/") + location + "?res=daily&key=75eea32c-ec7b-499f-9d47-a1ab760bf8da";
  return getWeb ((char*)"datapoint.metoffice.gov.uk", 80, weatherReq, "", msg);
}


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

/*
  bool getSunMoonx() {
  // http://aa.usno.navy.mil/data/docs/api.php
  String s;
  if (getWeb((char*)"api.usno.navy.mil", 443, "/rstt/oneday?id=ceibwr01&date=today&coords=52.07,-4.75", "", s)) {
    bool ok = extractSunMoonTime(s, "\"sundata", "\"R\"", sunRise);
    extractSunMoonTime(s, "\"sundata", "\"S\"", sunSet);
    extractSunMoonTime(s, "\"sundata", "\"U\"", midday);
    extractSunMoonTime(s, "\"nextmoondata", "\"R\"", moonRise)
    || extractSunMoonTime(s, "\"moondata", "\"R\"", moonRise);
    extractSunMoonTime(s, "\"nextmoondata", "\"S\"", moonSet)
    || extractSunMoonTime(s, "\"moondata", "\"S\"", moonSet);
    return ok;
  }
  else return false;
  }

  bool extractSunMoonTime(String &s, const char* body, const char *phen, float &hour) {
  int bx = s.indexOf(body);
  if (bx < 0) return false;
  int ex = s.indexOf("]", bx);
  int phenx = s.indexOf(phen, bx);
  if (phenx < 0 || phenx > ex) return false;
  int tx = s.indexOf("time\":", phenx) + 7;
  if (tx < 7) return false;
  int h = s.substring(tx, tx + 2).toInt();
  int m = s.substring(tx + 3, tx + 5).toInt();
  hour = h + m / 60.0;
  if (isSummertime()) hour += 1.0;
  return true;
  }

  bool getSunMoonx2() {
  String s;
  String req = "/weatherapi/sunrise/3.0/sun?lat=52.07&lon=-4.75&date=20{0}-{1}-{2}&offset=+00:00";
  req.replace("{0}", d2(rtc.getYear()));
  req.replace("{1}", d2(rtc.getMonth()));
  req.replace("{2}", d2(rtc.getDay()));
  if (getWeb((char*)"api.met.no", 443, req, "", s)) {
    if (!extractSunMoonXml(s, "moonrise", moonRise)) moonRise = 0.0;
    if (!extractSunMoonXml(s, "moonset", moonSet)) moonSet = 23.99;
    if (moonRise > moonSet) moonSet += 1.1; // show tomorrow's setting
    return extractSunMoonXml(s, "sunrise", sunRise) &&
           extractSunMoonXml(s, "sunset", sunSet);
  }
  else return false;
  }

*/

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


String location = "";

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

/*
   Heating control

*/


// Switch on or off
bool Heating::switchHeating() {
  float periodThisHour = period[rtc.getHours()];
  if (serviceOn > 0 && serviceOn < millis()) serviceOn = 0;
  bool shouldBeOn =
    !serviceOff && (serviceOn > 0 ||
                    rtc.getMinutes() < periodThisHour);
  if (shouldBeOn != isHeatingOn) {
    digitalWrite(HEAT_PIN, (shouldBeOn ? HIGH : LOW));
    isHeatingOn = shouldBeOn;
    clogn(isHeatingOn ? "ON" : "OFF");
    return true;
  }
  return false;
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

void Heating::setPeriods(float avgTempDiff)
{
  float seasonal = max(0.2, min(2.0, 1.0 + cosMonth[(rtc.getMonth() + 11) % 12] * insolationFactor)); // insolation of house

  if (avgTempDiff < 0) {
    for (int i = 0; i < 24; i++) period[i] = 0.0;
    period[3] = 10;
    dlogn(String("Negative deficit, setting minimal heating"));
    return ;
  }


  float weightSum = 0;
  for (int i = 0; i < 24; i++)
  {
    weightSum += hourlyWeights[i];
  }
  float excess = 0.0;
  float avgWeight = weightSum / 24.0;
  float factor = checkLowUntil () // On holiday?
                 ? lowHoursPerDay * 60 / weightSum  // Anti-condensation regime for holidays
                 : avgTempDiff * minsPerDegreePerHour * seasonal / avgWeight; // Spread across hours

  dlogn(String("Season: ") + seasonal + " minsPerDegreePerHour: " + minsPerDegreePerHour + " => mins: " + factor * avgWeight);

  for (int i = 0; i < 24; i++) {
    period[i] =  factor * hourlyWeights[i];
    if (period[i] > 50) {
      excess += period[i] - 50;
      period[i] = 50;
    }
  }
  if (excess > 0.01) { // cold period, add heat to night
    for (int i = 18; i >= 0; i--) {
      if (hourlyWeights[i] < 0.1) {
        float extra = min(30.0 - period[i], excess);
        period[i] += extra;
        excess -= extra;
        if (excess < 0.1) break;
      }
    }
  }
  // shift small amounts to next
  for (int i = 0; i < 23; i++) { // stop short
    if (period[i] < minimumBurst)
    {
      period[i + 1] += period[i];
      period[i] = 0.0;
    }
  }
  // Add time it takes pump to start
  for (int i = 0; i < 24; i++) {
    if (period[i] > 0.01) period[i] += startupTime;
  }
}

// Have we set the heating low until a specified date?
bool checkLowUntil () {
  if (lowUntilDate.length() > 0) {
    long ludY = lowUntilDate.substring(0, 2).toInt();
    long ludM = lowUntilDate.substring(3, 5).toInt();
    long ludD = lowUntilDate.substring(6).toInt();
    long rtcYMD = rtc.getYear() * 10000 + rtc.getMonth() * 100 + rtc.getDay();
    if (rtcYMD >= ludY * 10000 + ludM * 100 + ludD)  {
      lowUntilDate = "";
      dlogn("Cancelled vacation");
      saveParams();
    }
  }
  return lowUntilDate.length() > 0;
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
      if (server.status() == 0) {
        server.begin();
      }
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

/*
   Date time

*/

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


void Page::drawStatus () {
  showStatus(timeString().substring(0, 14) + "  " + (heating.isHeatingOn ? "ON" : "OFF"));
}


void showIP() {
  showStatus(ipString("") + " " + (heating.isHeatingOn ? "ON" : "OFF"));
}


void Temperatures:: record() {
  sumOverPeriod += getCurrent();
  periodCount++;
  if (millis() > previousRecord + 600000) {
    previousRecord = millis();
    if (periodCount > 0) {
      float avgTempOverPeriod = round(10 * sumOverPeriod / periodCount) / 10;
      File f = SD.open("TEMPERATURES.TXT", FILE_REWRITE);
      if (f) {
        f.println(timeString() + " " + (heating.isHeatingOn ? 1 : 0) + " " + avgTempOverPeriod);
        f.close();
      }
      periodCount = 0;
      sumOverPeriod = 0;
    }
  }
}

String Weather::codes[30] =
    { "stars", "sun", "cloud", "cloud", "", "mist", "fog", "Cloud", "ocast", "shwr", "shwr", "drizl",
      "rain", "Shwr", "Shwr", "RAIN",
      "sleet", "sleet", "Sleet", "hail", "hail",
      "snow", "snow", "snow", "Snow", "Snow", "SNOW",
      "thndr", "thndr", "Thndr"
    };

String dayNames [] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
String dayName(int i) {
  return dayNames[i];
}

// Let the watchdog time out
void softReboot() {
  dlogn("Reboot");
  sodaq_wdt_enable();
  while (true);
}
