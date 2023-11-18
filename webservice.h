#ifndef UF_WEBSERVICE
#define UF_WEBSERVICE
#include <Arduino.h>
#include "logger.h"

#include <WiFiNINA.h>   // on-board wifi
#include <WiFiUdp.h>


class WebService {
    WiFiServer server;
    void serveClient(WiFiClient&);
  public:
    WebService() : server(80){}
    void start() {
      if (server.status() == 0) {
        server.begin();
      }
    }
    void loop () {
      WiFiClient sclient = server.available();
      if (sclient) {
        clogn("server client");
        serveClient(sclient);
      }
    }
};

#endif
