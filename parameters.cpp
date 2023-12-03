#include "parameters.h"
#include "utility.h"
#include "logger.h"
#include "inside.h"
#include <SD.h>

extern Heating heating;

int webClientTimeout = 12000;  // ms
int updateHour = 3; // AM and PM weather update
float targetTemp = 20;
float lowHoursPerDay = 0.5;  // Hours per day to run when away, to avoid condensation
float avgFactor = 0.7; // skew across forecast days: raise -> today has more influence
float windSpeedFactor = 0.1; // temp deficit multiplier per 100 mph
float minsPerDegreePerHour = 0.8;


float hourlyWeights[24] =
{ 0, 0, 0, 0, 0, 0,
  1, 1, 1, 1, 0.5, 0.5,
  0.5, 0.5, 1, 1, 1, 1,
  0, 0, 0, 0, 0, 0
};

void updateParameters(String& content) {
  clogn (String("UPDATE: ") + content);
  getFProp(content, "target", 4.0, 40.0, targetTemp);
  String lud = getProp(content, "vacation", 1, 10000);
  lud.trim();
  if (lud.length() == 10) lud = lud.substring(2); // want two-digit year
  if (lud.length() == 0) heating.lowUntilDate = "";
  else if (lud.length() == 8 && lud.charAt(2) == '-' && lud.charAt(5) == '-') {
    heating.lowUntilDate = lud;
    //checkLowUntil(); // cancel if already past // don't save params while open for reading! <<<< FIX TODO
  }
  getFProp(content, "factor", 0.01, 10.0, minsPerDegreePerHour);
  getFProp(content, "skew", 0.01, 1.0, avgFactor);
  getFProp(content, "lowHoursPerDay", 0.1, 4.0, lowHoursPerDay);
  getFProp(content, "windSpeedFactor", 0.1, 1.0, windSpeedFactor);
  float udh = -1.0;
  getFProp(content, "updateHour", 0, 23.99, udh);
  if (udh >= 0.0) updateHour = (int)udh;
  const String profileString ("\"profile\":[");
  int ix = content.indexOf(profileString);
  if (ix >= 0) {
    ix += profileString.length();
    for (int i = 0; i < 24; i++) {
      float w = content.substring(ix).toFloat();
      if (w >= 0.0 && w < 10)
        hourlyWeights[i] = w;
      ix = content.indexOf(",", ix) + 1;
      if (ix <= 0) break;
    }
  }
}

void getFProp(String &msg, String propName, float minf, float maxf, float & propVar) {
  String p = getProp(msg, propName, 1, 10000);
  if (p.length() > 0) {
    float t = p.toFloat();
    if (t >= minf && t <= maxf) propVar = t;
  }
}


// Save persistent data to SD card
void saveParams() {
  File f = SD.open("P.TXT", FILE_REWRITE);
  if (f) {
    String s;
    outParams(s);
    f.print(s);
    f.close();
    dlogn("Saved parameters");
  }
  else dlogn("Couldn't write parameters");
}

// Get persistent data from SD card
void getParams() {
  String bb = getShortFileContent("P.TXT");
  if (bb.length()>0) {
    dlogn(String("Parameters from file:") + bb);
    updateParameters(bb);
  } else {
    dlogn(String("No saved parameters - using defaults"));
  }
}


String outWeights() {
  String weights = "";
  for (int i = 0; i < 24; i++) {
    if (i % 6 == 0) {
      weights += "\n";
    }
    weights += String(hourlyWeights[i], 2) + (i < 23 ? "," : "");
  }
  return weights;
}

String outParams() {
  String t = String("{'target':%0,'vacation':'%1','factor':%2,'skew':%3,'updateHour':%4,'lowHoursPerDay':%5,'windSpeedFactor':%6,'profile':[%7]}");
  t.replace('\'', '"');
  t.replace("%0", String(targetTemp, 1));
  t.replace("%1", heating.lowUntilDate);
  t.replace("%2", String(minsPerDegreePerHour, 2));
  t.replace("%3", String(avgFactor, 2));
  t.replace("%4", String(updateHour));
  t.replace("%5", String(lowHoursPerDay));
  t.replace("%6", String(windSpeedFactor));
  t.replace("%7", outWeights());
  return t;
}

void outParams (String&msg) {
  msg += outParams();
}
