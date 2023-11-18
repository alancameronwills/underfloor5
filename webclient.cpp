#include "webclient.h"
#include "webservice.h"
#include "parameters.h"

#include <Sodaq_wdt.h>  // watchdog

extern WebService webservice;
extern bool isProtoBoard;

bool sendWebReq (WiFiClient &client, char* host, int port, String request, String extraLine) {
  if (WiFi.status() != WL_CONNECTED) {
    if (!webservice.connectWiFi()) return false;
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

void webIndicator(bool onOrOff)
{
  digitalWrite(6, onOrOff ? HIGH : LOW);
  WiFiDrv::analogWrite(26, onOrOff ? 255 : 0);
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


bool getWeb(char* host, int port, String request, String extraLine, String& response) {
  WiFiClient client;
  return getWeb(host, port, request, extraLine, response, client);
}
