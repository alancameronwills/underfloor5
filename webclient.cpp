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

  clogn(String("WEB REQ ") + host + ":" + port);
  long begin = millis();
  sodaq_wdt_reset();
  if (port == 443 ? client.connectSSL(host, port) : client.connect(host, port))
  {
    // clog(String(millis() % 100000) + "+");
    client.print(req);
    clogn(String("  SENT ") + (millis()-begin)/1000.0);
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
  clogn(String("Get sync ") + host + " took: " + ((unsigned long)(millis() - startTime) / 1000.0) + (timedOut ? " timed out" : ""));
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
    char *host;
    int port;
    String request;
    String extraLine;
    int braceMax = 1000;
    bool active = false;
    bool waitingToSend = false;
    bool ready = false;
    int status = 0;
    long contentLengthExpected = -1;
    long charCount = 0;
    long startTime = 0;
    long contentStart = 0;
    long headerExaminedTo = 0;
    WiFiClient client;
    String response;
    WebResponseHandler *handler;

  public:
    void clear()
    {
      active = false;
      waitingToSend = false;
      ready = false;
      client.stop();
      response = "";
      contentLengthExpected = -1;
      headerExaminedTo = 0;
      contentStart = 0;
      charCount = 0;
      braceMax = 1000;
    }
    bool isWaiting() {
      return active && waitingToSend;
    }
    bool isActive() {
      return active;
    }
    
    bool queueReq(char *_host, int _port, String _request, String _extraLine, int braceLimit, WebResponseHandler *responseHandler){
      clear();
      host = _host;
      port = _port;
      request = _request;
      extraLine = _extraLine;
      braceMax = braceLimit;
      handler = responseHandler;
      active = true;
      waitingToSend = true;
    }

    bool sendReq()
    {
      waitingToSend = false;
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
    bool loop () {
      if (!active || waitingToSend) return false;
      if (!client.connected() || ready) {
        clogn(String ("Async get ") + host
              + " took: " + (millis() - startTime)/1000.0 + " header: " + contentStart + " content: " + contentLengthExpected);
        handler->gotResponse(status, response);
        clear();
        return false;
      }
      while (client.available() && !ready) {
        char c = client.read();
        charCount++;
        ready = c=='}' && braceMax-- <= 0 ||
          contentLengthExpected >= 0 && charCount >= contentLengthExpected ||
          millis() - startTime > webClientTimeout;
        if (c == '\r') continue;
        response += c;
        if ((int)c < 32 && c != '\n' && c != '\r' && c != '\t') {
          ready = true;
          clogn(String ("CTRL ") + (int)c);
          break;
        }
        long responseLength = response.length();
        if (c == '\n' && contentStart == 0 && responseLength >= 2)
        {
          if (headerExaminedTo == responseLength-1) {
            // empty line ==> end of header
            contentStart = responseLength;
            if (contentLengthExpected > 0) charCount = 0;
          } else if (contentLengthExpected < 0 && contentStart == 0)
          {
            String s = response.substring(headerExaminedTo);
            s.toLowerCase();
            int icl = s.indexOf("content-length:");
            if (icl >= 0)
            {
              contentLengthExpected = s.substring(icl + 16).toInt();
              clogn(String("@Content-Length: ") + contentLengthExpected);
            }
          }
          headerExaminedTo = responseLength;
        }
      }
      return true;
    }
};
const int WebReqMaxActive = 2;
const int WEBREQLISTSSIZE = 4;
WebRequest webReqList[WEBREQLISTSSIZE];

bool getWebAsync(char *host, int port, String request, String extraLine, WebResponseHandler *responseHandler, int braceLimit)
{
  int reqix = 0;
  while (reqix < WEBREQLISTSSIZE && webReqList[reqix].isActive())
    reqix++;
  if (reqix >= WEBREQLISTSSIZE)
    return false;
  return webReqList[reqix].queueReq(host, port, request, extraLine, braceLimit, responseHandler);
}

void webClientLoop()
{
  int waitingForResponses = 0;
  WebRequest* nextUp = nullptr;
  for (int i = 0; i < WEBREQLISTSSIZE; i++)
  {
    if (webReqList[i].loop()) waitingForResponses++;
    else if (webReqList[i].isWaiting()) nextUp = &(webReqList[i]);
  }
  webIndicator(waitingForResponses);
  if (waitingForResponses<WebReqMaxActive && nextUp != nullptr) {
    nextUp->sendReq();
  }
}


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
    timeInSeconds = getWiFiTime();
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
