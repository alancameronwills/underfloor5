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

class Tidal : public WebResponseHandler {
    bool getSunMoonAsync(SunMoonResponseHandler *responder, void (*done)(bool success));
    void (*done)(bool success);
    bool parseTides(String &msg);
  public:
  /*WebResponseHandler*/
    void gotResponse(int status, String content);
  /*Tidal*/
    Tide tides [4];
    bool getTidesAsync(void (*done)(bool success));
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

class Weather : public WebResponseHandler {
    String location = "";
    static String codes [30];
    void (*gotWeather)(bool);
  public:
    void gotResponse(int, String);
    WeatherDay forecast[WEATHER_DAYS];
    String code(int i) {
      return codes[i];
    }
    String weatherReport() ;
    bool parseWeather(String& msg);
    float getForecastTempDiff(float targetTemp);
    bool getWeatherForecast(String& msg);
    bool getWeatherForecastAsync();
    bool getWeather(void(*_gotWeather)(bool));

};

#endif
