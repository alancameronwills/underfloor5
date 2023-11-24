#ifndef UF_OUTSIDE
#define UF_OUTSIDE

#include <Arduino.h>
#include "webclient.h"

class Tidal;
class SunMoonResponseHandler : public WebResponseHandler {
  protected:
    Tidal *tidal;
    String sunOrMoon;
    void (*doneSunMoon)(bool);
    bool extractRiseSet (String s, String id, float &hour);
    
    void checkDone();
  public:
    void setDoneHandler(void (*handler)(bool)) {
      doneSunMoon = handler;
    }
    virtual String id()=0;
};
class SunResponseHandler : public SunMoonResponseHandler {
  public:
    SunResponseHandler(Tidal *t) {
      tidal = t;
    }
    void gotResponse(int status, String content);
    String id() {
      return "sun";
    }
};
class MoonResponseHandler : public SunMoonResponseHandler {
  public:
    MoonResponseHandler(Tidal *t) {
      tidal = t;
    }
    void gotResponse(int status, String content);
    String id() {
      return "moon";
    }
};


struct Tide {
  String eventType;
  int day;
  float tod;
  float height;
};

class Tidal {
    bool getSunMoonAsync(SunMoonResponseHandler *responder, void (*done)(bool success));
  public:
    Tide tides [4];
    bool parseTides(String &msg);
    bool getTides();
    String tidesReport() ;
    
    float sunRise, sunSet, midday, moonRise, moonSet;
    bool getSunMoonAsync(void (*done)(bool success));
};




class WeatherDay {
  public:
    String fcDate;
    String tempMin;
    String tempMax;
    String windDirection;
    String windSpeed;
    String precip;
    String precipN;
    String weather;
    String report ();
};

#define WEATHER_DAYS 5


#define WEATHER_DAYS 5
class Weather {
    String location = "";
    static String codes [30];
  public:
    WeatherDay forecast[WEATHER_DAYS];
    String code(int i) {
      return codes[i];
    }
    String weatherReport() ;
    bool parseWeather(String& msg);
    float getForecastTempDiff(float targetTemp);
    bool getWeatherForecast(String& msg);
    bool getWeather();

};

#endif
