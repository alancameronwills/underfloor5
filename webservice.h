#ifndef UF_WEBSERVICE
#define UF_WEBSERVICE
#include <Arduino.h>
#include "logger.h"

#include <WiFiNINA.h>   // on-board wifi
#include <WiFiUdp.h>


class WebService {
    WiFiServer server;
    void serveClient(WiFiClient&);

    unsigned long connectFailStart = 0;
    const int embargoPeriod = 15; // minutes
    int embargoUntil = -1; // ToD in minutes to resume attempts
    bool checkReconnect();
  public:
    WebService() : server(80) {}
    bool connectWiFi ();
    void start() ;
    void loop () {
      WiFiClient sclient = server.available();
      if (sclient) {
        clogn("server client");
        serveClient(sclient);
      }
    }
    String macAddress() {
      byte mac[6];
      WiFi.macAddress(mac);
      return String("") + mac[0];
    }

    String ipString(const char *c);
};

#endif
