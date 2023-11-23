#include <Arduino.h>


bool getWeb(char* host, int port, String request, String extraLine, String& response);

void setTimeFromWiFi();
unsigned long getWiFiTime();

// Remind router we're here
void pingConx() ;

/**
 * Abstract class for responses to web requests.
*/
class WebResponseHandler {
	public:
		void gotResponse(int status, String content);
};

bool getWebAsync(char* host, int port, String request, String extraLine, WebResponseHandler *responseHandler);
