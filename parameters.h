#ifndef UF_PARAMS
#define UF_PARAMS
#include <Arduino.h>


extern int webClientTimeout;  // ms
extern int updateHour; // AM and PM weather update
extern float targetTemp;
extern float lowHoursPerDay;  // Hours per day to run when away, to avoid condensation
extern float avgFactor; // skew across forecast days: raise -> today has more influence
extern float windSpeedFactor; // temp deficit multiplier per 100 mph
extern float minsPerDegreePerHour;
extern float hourlyWeights[24];

void updateParameters(String& content) ;
void getFProp(String &msg, String propName, float minf, float maxf, float & propVar) ;
void saveParams() ;
void getParams() ;
void outParams (String&msg) ;
String outParams();

#endif
