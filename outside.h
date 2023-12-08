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
    virtual String id() = 0;
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

class Weather : public WebResponseHandler {
    String location = "";
    static String codes [30];
    void (*gotWeather)(Weather*);
    unsigned long forecastTimestamp = 0; // seconds when got from web
    /** Hours since real forecast obtained, or 0 == never == forecast invalid*/
    int weatherAge();
    WeatherDay forecast[WEATHER_DAYS];
    bool getWeatherForecastAsync();
    bool tparseWeather(String& msg, unsigned long timestamp);
    bool parseWeather(String& msg, unsigned long timestamp);

    Weather& operator= (const Weather &w) {
      if (this != &w) {
        location = w.location;
        forecastTimestamp = w.forecastTimestamp;
        for (int i = 0; i < WEATHER_DAYS; i++) {
          forecast[i] = w.forecast[i];
        }
      }
      return *this;
    }

  public:
    void gotResponse(int, String);
    String code(int i) {
      return codes[i];
    }
    String weatherReport() ;
    WeatherDay* getWeatherDay(int i) {
      return &forecast[i];
    }

    /** Average temp difference in next few days */
    float getForecastTempDiff(float targetTemp);

    /** Ensure the forecast is more or less up to date
       then call _gotWeather
    */
    bool useWeatherAsync(void (*_gotWeather)(Weather*));
};

#endif
