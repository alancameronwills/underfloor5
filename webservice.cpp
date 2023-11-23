#include "webservice.h"
#include "utility.h"
#include "logger.h"
#include "inside.h"
#include "outside.h"

#include <utility/wifi_drv.h> // for indicator lamp WiFiDrv
#include <RTCZero.h>
#include <Sodaq_wdt.h>  // watchdog


#define WIFI_SSID_COUNT 2
String wifiSSID[] = {"Pant-y-Wylan", "Pant-2-Wylan"};
int wifiSelected = 0;

unsigned long connectFailRebootTimeout = 2 * 60 * 60 * 1000L; // 2 hours

extern Tidal tidal;
extern Weather weather;
extern Temperatures temperatures;
extern Heating heating;
extern RTCZero rtc;


extern float targetTemp;
extern float avgDeficit;
extern bool logging;
extern bool debugging;


extern float hourlyWeights[];
extern float minsPerDegreePerHour;
extern float lowHoursPerDay;

float checkLowUntil ();
String outParams();
void outParams (String&msg);
void doItNow();
void softReboot();
void saveParams();
void updateParameters(String& content);

void WebService::start() {
  if (server.status() == 0) {
    server.begin();
    WiFiDrv::pinMode(25, OUTPUT); //onboard LED green
    WiFiDrv::pinMode(26, OUTPUT); //onboard LED red
    WiFiDrv::pinMode(27, OUTPUT); //onboard LED blue
  }
}


// WiFi server

// Set on or off immediately for service purposes
void setOffOrOn (String request) {
  heating.serviceOff = false;
  heating.serviceOn = 0;
  if (request.indexOf("?set=off ") > 0) heating.serviceOff = true;
  else if (request.indexOf("?set=on ") > 0) heating.serviceOn = millis() + 30 * 60 * 1000;
  doItNow();
}

void adjustTargetTemp(float t) {
  if (t > 8.0 && t < 30)
  {
    targetTemp = t;
    saveParams();       // persist to disc
    doItNow();
  }
}

void respondParameterUpdate(WiFiClient& client, String &request, String &content) {
  updateParameters(content);
  saveParams();       // persist to disc
  doItNow();

  //client.println("HTTP/1.1 303 See other");
  //client.println("Location: /");
  //client.println("");
  client.println("HTTP/1.1 200 OK");
  client.println("");
  client.println("<!DOCTYPE HTML>");
  client.println("<html><head><meta http-equiv=\"refresh\" content=\"10;URL='/'\"/></head><body>Updating...</body></html>");
}


int unhex(char c) {
  return c < 'A' ? c - '0' : c - 'A' + 10;
}

bool saveTemplate(WiFiClient &client, int contentLength) {
  clog("save");
  File f = SD.open("TEMPLATE.HTM", FILE_REWRITE);
  if (!f) return false;
  clog("~");
  int count = 0;
  char code[2];
  int codeChar = 0;
  bool started = false;
  while (client.connected() && client.available() && count++ < contentLength) {
    char c = client.read();
    if(!started) {
      if (c=='=') started = true;
      continue;
    }
    
    if (c == '%') {
      codeChar = 1;
    } else if (codeChar > 0) {
      code[codeChar++ -1] = c;
      if (codeChar>2) {
        codeChar= 0;
        c = (char)(unhex(code[0])*16 + unhex(code[1]));
        f.write(c);
      }
    } else {
      if (c=='+') c = ' ';
      f.write(c);
    }
  }
  f.close();
  clogn("#");
  return started;
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

/**
   Use for short content blocks. Anything too big busts the variable memory.
*/
void getContentFromClient(WiFiClient &client, int lengthExpected, String &content) {
  int count = 0;
  while (client.connected() && client.available() && count++ < lengthExpected) {
    content += (char)client.read();
  }
  decode(content);
}

/** Reads request header, leaves client open to read content
   @param request out - request header
   @returns length of content yet to be read
*/
int getRequestFromClient(WiFiClient& client, String &request) {
  boolean currentLineIsBlank = true;
  int contentLength = 0;
  bool readingContent = false;
  int timeout = 500;
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();

      request += c;
      // Blank line terminates header:
      if (c == '\n' && currentLineIsBlank) {
        // parse request
        int ix = request.indexOf("Content-Length:");
        if (ix >= 0) {
          contentLength = request.substring(ix + 15).toInt();
          clogn(String("content ") + contentLength);
        }
        break;
      }
      if (c == '\n') {
        // starting a new line
        currentLineIsBlank = true;
      }
      else if (c != '\r') {
        currentLineIsBlank = false;
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
  clogn("#");
  clogn(request);
}


// Web page icon is a single block of colour. The three bytes last-8..6 are the icon colour. K==0, J==255.
char* icoCode = (char *)"KKLKLKLLKKLKcK{KKKaKKKsKKKLKKKMKKKLKcKKKKKKKKKKKKKKKKKKKKKKKKKzJzKKKKK";
byte icobytes [70];
void respondIcon (WiFiClient& client, String& req)
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


int findParameter(char *buf, int start, int end) {
  for (int ix = start; ix < end; ix++) {
    if (buf[ix] == '{' && buf[ix + 1] == '%') return ix;
  }
  return -1;
}


// Create a web page by substituting static var values into a template file.
// Current parameters are: {%temp} {%factor} {%vacation}
// Template file is template.htm
void respondT(WiFiClient& client, String& req) {
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
          String value = paramName.equalsIgnoreCase("target") ? String(targetTemp, 1) :
                         paramName.equalsIgnoreCase("factor") ? String(minsPerDegreePerHour) :
                         paramName.equalsIgnoreCase("vacation") ? heating.lowUntilDate :
                         paramName.equalsIgnoreCase("serviceState") ? String(heating.serviceOff ? "SERVICE OFF" : heating.serviceOn > 0 ? "SERVICE ON" : "") :
                         paramName.equalsIgnoreCase("params") ? outParams() :
                         paramName.equalsIgnoreCase("current") ? String(temperatures.getCurrent(), 1) :
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

void logPage(WiFiClient& client, const String& logfile = "LOG.TXT") {
  char buf [1000];
  client.println("<a href='/'>Home</a>");
  client.println(String("<h2>-") + logfile + "-</h2>");
  client.println("<pre>");
  File f = SD.open(logfile);
  int count = 0;
  if (f) {
    while (f.available()) {
      int cc = f.read(buf, 1000);
      if (cc > 0) {
        client.write(buf, cc);
      } else {
        delay (1);
        if (count++>1000) break;
      }
    }
    f.close();
  }
  else client.println("Can't open log file");
  client.println("</pre><a href='/'>Home</a><br/><br/><a href='/delete'>Delete log</a><br/><br/><a href='/reboot'>Reboot</a>");
}

void deleteLogPage(WiFiClient& client, const String& logfile = "LOG.TXT") {
  client.println(String("<p>") + clearFile(logfile) + "</p>");
  client.println("<a href='/'>Home</a><br/>");
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

void statusPage(WiFiClient& client, bool isUpd) {
  client.println("<pre>");
  String body = timeString() + "   " + (heating.serviceOff ? "SERVICE OFF  " : heating.serviceOn > 0 ? "SERVICE ON  " : "")
                + (heating.isHeatingOn ? "ON" : "OFF") + "  " + temperatures.getCurrent() + "\n\n\n"
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

void respond(WiFiClient& client, String& req, int contentLength) {
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
    logPage(client, "TEMPERAT.TXT");
  } else if (req.indexOf(" /file?") > 0) {
    int ix = req.indexOf("?");
    int jx = req.indexOf(" ", ix + 1);
    logPage(client, req.substring(ix + 1, jx));
  } else if (req.indexOf(" /delete") > 0) {
    deleteLogPage(client);
    deleteLogPage(client, "TEMPERAT.TXT");
  } else if (req.indexOf(" /reboot") > 0) {
    client.println("<b>Rebooting</b>");
  } else if (req.indexOf("POST /template") >= 0) {
    if (saveTemplate(client, contentLength)) client.println("Template saved");
    else client.println("Couldn't save template");
  } else if (req.indexOf("GET /template") >= 0) {
    client.println("<form method='POST'><textarea name='x'></textarea><input type='submit'/></form>");
  } else {
    statusPage(client, req.indexOf(" /upd") > 0); // GET or POST but not Referer
  }
  client.println("</body></html>");
}



void WebService::serveClient(WiFiClient& client)
{
  String request;
  int contentLengthToGet = getRequestFromClient(client, /*out*/ request);
  if (contentLengthToGet > 0 && (request.indexOf("POST /upd ") > 0 || request.indexOf("POST / ") >= 0)) {
    String content;
    getContentFromClient(client, contentLengthToGet, /*out*/ content);
    respondParameterUpdate(client, request, content);
  }
  else if (request.indexOf("favicon.ico") > 0) respondIcon(client, request);
  else if (request.indexOf(" / ") > 0) respondT(client, request);
  else if (request.indexOf(" /service?") > 0) {
    setOffOrOn(request);
    respondT(client, request);
  }
  else respond(client, request, contentLengthToGet);
  delay(1);
  client.flush();
  client.stop();
  clogn("Disconnected");
  if (request.indexOf(" /reboot") > 0) softReboot();
}



bool WebService::connectWiFi ()
{
  const IPAddress ip(192,168,1,90);
  const IPAddress dns1(8,8,8,8);
  const IPAddress dns2(8,8,4,4);
  const IPAddress gateway(192,168,1,1);
  const IPAddress mask(255,255,255,0);
  if (WiFi.status() != WL_CONNECTED)
  {
    if (!checkReconnect()) {
      return false;
    }
    clog("Connecting");
    WiFi.config(ip, dns1, gateway,mask);
    WiFi.setHostname("heating.local");
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
      start();
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

bool WebService::checkReconnect() {
  if (logging || debugging) return true;
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

String WebService::ipString(const char *c) {
  IPAddress ip = WiFi.localIP();
  return String(c) + ip[0] + "." + ip[1] + "." + ip[2] + "." + ip[3];
}
