#ifndef UF_OUTSIDE
#define UF_OUTSIDE

#include <Arduino.h>


struct Tide {
  String eventType;
  int day;
  float tod;
  float height;
};

class Tidal {
  public:
    Tide tides [4];

    bool parseTides(String &msg);
    bool extractRiseSet (String s, String id, float &hour);
    float sunRise, sunSet, midday, moonRise, moonSet;

    bool getSunMoon();

    bool getSunMoon(String sunOrMoon, float &riseHour, float &setHour);

    bool getTides();
    String tidesReport() ;

    bool getSunMoonAsync();

    bool getSunMoonAsync(String sunOrMoon);
    
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
