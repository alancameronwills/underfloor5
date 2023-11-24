#ifndef UF_WEBCLIENT
#define UF_WEBCLIENT

#include <Arduino.h>

bool getWeb(char* host, int port, String request, String extraLine, String& response);

void setTimeFromWiFi();
unsigned long getWiFiTime();

// Remind router we're here
void pingConx() ;

void webClientLoop();

/**
 * Abstract class for responses to web requests.
*/
class WebResponseHandler {
	public:
		virtual void gotResponse(int status, String content)=0;
};

bool getWebAsync(char* host, int port, String request, String extraLine, WebResponseHandler *responseHandler);

#endif
