#ifndef UF_INSIDE
#define UF_INSIDE

#include <Arduino.h>


#define HEAT_PIN 7  // Heating power relay
#define THERMISTOR A6



class Heating {
  public:
    float totalHours = 0.0;       // Heating per day
    float period [24];            // Minutes in each hour to run heating
    bool isHeatingOn = false;

    void setPeriods(float avgTempDiff);
    void setOffOrOn(String request);
    bool switchHeating();
    /* for Screen */
    bool serviceOff = false;
    long serviceOn = 0;
    String lowUntilDate = "";

    void setup() ;

    String periodsReport();
    String shortPeriodsReport() ;
};

class Temperatures {
    long previousRecord = 10000;
    float sumOverPeriod = 0;
    long int periodCount = 0;
  public:
    float getCurrent() {
      int v = analogRead(THERMISTOR);
      return (v - 250) / 10.4;
    }
    void record() ;
};


#endif
