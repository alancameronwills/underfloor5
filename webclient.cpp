#include "webclient.h"
#include "webservice.h"
#include "parameters.h"
#include "utility.h"
#include <RTCZero.h> // clock

#include <Sodaq_wdt.h> // watchdog

extern WebService webservice;
extern RTCZero rtc;

bool sendWebReq(WiFiClient &client, char *host, int port, String request, String extraLine)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    if (!webservice.connectWiFi())
      return false;
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
    // clog(String(millis() % 100000) + "+");
    client.print(req);
    clogn("  SENT");
    return true;
  }
  else
  {
    clogn("Failed to connect");
    return false;
  }
}

void webIndicator(bool onOrOff)
{
  digitalWrite(6, onOrOff ? HIGH : LOW);
  WiFiDrv::analogWrite(26, onOrOff ? 255 : 0);
}

bool getWeb(char *host, int port, String request, String extraLine, String &response, WiFiClient &client)
{
  bool detail = false;
  bool tides = String(host).indexOf("easytide") >= 0;
  webIndicator(true);
  int charCount = 0; // including CRs
  int braceCount = 0;
  int headerEnd = 0; // start of body
  const int contentLengthMax = 20000;
  int contentLength = contentLengthMax; // read from Content-Length field
  bool lengthGot = false;               // read contentLength chars from body
  bool timedOut = false;
  unsigned long startTime = millis();
  // clog(String(startTime % 100000) + ";");

  if (!sendWebReq(client, host, port, request, extraLine))
    return false;
  response = "";
  clog(String("="));
  while (client.connected() && !timedOut && !lengthGot)
  {
    // clog(String(millis() % 100000) + ":");
    while (client.available() && client.connected() && !timedOut && !lengthGot)
    {
      // if (detail) clog(String(millis() % 100000) + "[");
      char c = client.read();
      // if (detail) clog(String(c));
      timedOut = (unsigned long)(millis() - startTime) > webClientTimeout;
      charCount++;
      if (contentLength > 0 && charCount >= contentLength)
        lengthGot = true;
      if (c == '\r')
        continue; // omit CRs except for counting
      response += c;
      if (c == '\n')
      {
        if (contentLength == contentLengthMax && headerEnd == 0)
        {
          clog("-");
          String s = String(response);
          s.toLowerCase();
          int icl = s.indexOf("content-length:");
          if (icl >= 0)
          {
            clog("#");
            contentLength = s.substring(icl + 16, icl + 24).toInt();
            clogn(String("Content-Length: ") + contentLength);
          }
        }
        else
        {
          clog("_");
        }
        if (headerEnd == 0 && charCount > 2 && response.charAt(response.length() - 2) == '\n')
        { // EOH
          headerEnd = charCount;
          charCount = 0;
        }
      }
      if (tides && c == '}' && braceCount++ > 8)
        lengthGot = true;
    }
    // clog(String(millis() % 100000) + "]");
    timedOut = (unsigned long)(millis() - startTime) > webClientTimeout;
    delay(10);
  }
  clogn(String("Get took: ") + ((unsigned long)(millis() - startTime) / 1000.0) + (timedOut ? " timed out" : ""));
  client.stop();
  webIndicator(false);
  clogn(response);
  return response.length() > 200;
}

bool getWeb(char *host, int port, String request, String extraLine, String &response)
{
  WiFiClient client;
  return getWeb(host, port, request, extraLine, response, client);
}

class WebRequest
{
  bool active = false;
  bool ready = false;
  int status = 0;
  unsigned long contentLength = 0;
  unsigned long charCount = 0;
  unsigned long startTime = 0;
  WiFiClient client;
  String response;
  WebResponseHandler *handler;

public:
  void clear()
  {
    active = false;
    ready = false;
    client.stop();
    response = "";
    contentLength = 0;
    charCount = 0;
  }
  bool isActive() {return active;}
  bool sendReq(char *host, int port, String request, String extraLine, WebResponseHandler *responseHandler)
  {
    clear();
    active = true;
    handler = responseHandler;
    startTime = millis();
    if (sendWebReq(client, host, port, request, extraLine))
    {
      return true;
    }
    else
    {
      clear();
      return false;
    }
  }
  void loop () {
    if (!active) return;
    if(!client.connected() || ready) {
      handler->gotResponse(status, response);
      clear();
      return;
    }
    while(client.available() && !ready) {
      char c = client.read();
      // if (detail) clog(String(c));
      charCount++;
      ready =  contentLength > 0 && charCount>=contentLength || millis() - startTime > webClientTimeout;
      if (c=='\r') continue;
      /* XXXXX */
    }
  }
};
const int WEBREQLISTSSIZE = 10;
WebRequest webReqList[WEBREQLISTSSIZE];

bool getWebAsync(char *host, int port, String request, String extraLine, WebResponseHandler *responseHandler)
{
  int reqix = 0;
  while (reqix < WEBREQLISTSSIZE && webReqList[reqix].isActive())
    reqix++;
  if (reqix >= WEBREQLISTSSIZE)
    return false;
  return webReqList[reqix].sendReq(host, port, request, extraLine, responseHandler);
}

/*
void loop()
{
  for (int i = 0; i < WEBREQLISTSSIZE; i++)
  {
    webReqList[i].loop();
  }
}
*/

unsigned long getWiFiTime()
{
  return WiFi.getTime();
}

void setTimeFromWiFi()
{
  if (WiFi.status() != WL_CONNECTED)
    return;
  rtc.begin();
  unsigned long timeInSeconds = 0;
  for (int tries = 0; tries < 20; tries++)
  {
    timeInSeconds = WiFi.getTime();
    if (timeInSeconds > 0)
      break;
    delay(500);
    digitalWrite(6, HIGH);
    delay(500);
    digitalWrite(6, LOW);
    sodaq_wdt_reset();
  }
  if (timeInSeconds > 0)
  {
    rtc.setEpoch(timeInSeconds);
    if (isSummertime())
    {
      rtc.setEpoch(timeInSeconds + 3600);
    }
    clogn("Got time");
  }
  else
    dlogn("Failed to get time");
}

// Remind router we're here
void pingConx()
{
  if (webservice.connectWiFi())
  {
    clog("ping ");
    unsigned long m = millis();
    WiFi.ping("google.co.uk");
    clogn(String(millis() - m));
  }
}
