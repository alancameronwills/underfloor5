#include <Arduino.h>


bool getWeb(char* host, int port, String request, String extraLine, String& response);

void setTimeFromWiFi();
unsigned long getWiFiTime();

// Remind router we're here
void pingConx() ;
