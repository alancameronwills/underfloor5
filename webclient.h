#include <Arduino.h>


bool getWeb(char* host, int port, String request, String extraLine, String& response);

void setTimeFromWiFi();

// Remind router we're here
void pingConx() ;
