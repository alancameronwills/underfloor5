#ifndef UF_WEBSERVICE
#define UF_WEBSERVICE
#include <Arduino.h>

#include <WiFiNINA.h>   // on-board wifi
#include <WiFiUdp.h>


class WebService {
  WiFiServer server;
  void serveClient(WiFiClient&);

  unsigned long connectFailStart = 0;
  const int embargoPeriod = 15; // minutes
  int embargoUntil = -1; // ToD in minutes to resume attempts
  bool checkReconnect();
  long previousConnectionAttempt = 0;
public:
  WebService() : server(80) {}
  void start();
  bool connectWiFi();
  void loop(unsigned long now);
  String macAddress();
  String ipString(const char* c);
};

#endif
