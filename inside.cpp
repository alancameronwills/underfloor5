#include "inside.h"
#include "utility.h"
#include <RTCZero.h>
#include "logger.h"
#include "parameters.h"

extern float checkLowUntil ();
extern RTCZero rtc;

float cosMonth[12] = {1.0, 0.7, 0.25, -0.2, -0.7, -1.0, -1.0, -0.7, -0.2, 0.25, 0.7, 1.0};
float insolationFactor = 0.4; // varies minsPerDegreePerHour from 0.4 to 1.2, winter to summer
float minimumBurst = 10.0; // minutes
float startupTime = 3.0; // minutes before heating actually starts

void Heating::setup() {
  isHeatingOn = false;
  pinMode(HEAT_PIN, OUTPUT); // Heating relay
  digitalWrite(HEAT_PIN, LOW); // LOW == OFF
}

String Heating::periodsReport() {
  String r;
  totalHours = 0.0;
  r += "Periods: \n";
  for (int i = 0; i < 24; i++) {
    r += d2((int)period[i]) + " ";
    totalHours += (period[i] / 60);
    if (i % 6 == 5) r += "\n";
  }
  r += "\nTotal hours: ";
  r += totalHours;
  return r;
}

String Heating::shortPeriodsReport() {
  String r ("Heat: ");
  totalHours = 0.0;
  for (int i = 0; i < 24; i++) {
    r += (int)(period[i] / 6 + 0.5);
    totalHours += (period[i] / 60);
    if (i % 6 == 5) r += " ";
  }
  r += " Total hours: ";
  r += totalHours;
  return r;
}
// Switch on or off
bool Heating::switchHeating() {
  float periodThisHour = period[rtc.getHours()];
  if (serviceOn > 0 && serviceOn < millis()) serviceOn = 0;
  bool shouldBeOn =
    !serviceOff && (serviceOn > 0 ||
                    rtc.getMinutes() < periodThisHour);
  if (shouldBeOn != isHeatingOn) {
    digitalWrite(HEAT_PIN, (shouldBeOn ? HIGH : LOW));
    isHeatingOn = shouldBeOn;
    clogn(isHeatingOn ? "ON" : "OFF");
    return true;
  }
  return false;
}

void Heating::setPeriods(float avgTempDiff)
{
  float seasonal = max(0.2, min(2.0, 1.0 + cosMonth[(rtc.getMonth() + 11) % 12] * insolationFactor)); // insolation of house

  if (avgTempDiff < 0) {
    for (int i = 0; i < 24; i++) period[i] = 0.0;
    period[3] = 10;
    dlogn(String("Negative deficit, setting minimal heating"));
    return ;
  }


  float weightSum = 0;
  for (int i = 0; i < 24; i++)
  {
    weightSum += hourlyWeights[i];
  }
  float excess = 0.0;
  float avgWeight = weightSum / 24.0;
  float factor = checkLowUntil () // On holiday?
                 ? lowHoursPerDay * 60 / weightSum  // Anti-condensation regime for holidays
                 : avgTempDiff * minsPerDegreePerHour * seasonal / avgWeight; // Spread across hours

  dlogn(String("Season: ") + seasonal + " minsPerDegreePerHour: " + minsPerDegreePerHour + " => mins: " + factor * avgWeight);

  for (int i = 0; i < 24; i++) {
    period[i] =  factor * hourlyWeights[i];
    if (period[i] > 50) {
      excess += period[i] - 50;
      period[i] = 50;
    }
  }
  if (excess > 0.01) { // cold period, add heat to night
    for (int i = 18; i >= 0; i--) {
      if (hourlyWeights[i] < 0.1) {
        float extra = min(30.0 - period[i], excess);
        period[i] += extra;
        excess -= extra;
        if (excess < 0.1) break;
      }
    }
  }
  // shift small amounts to next
  for (int i = 0; i < 23; i++) { // stop short
    if (period[i] < minimumBurst)
    {
      period[i + 1] += period[i];
      period[i] = 0.0;
    }
  }
  // Add time it takes pump to start
  for (int i = 0; i < 24; i++) {
    if (period[i] > 0.01) period[i] += startupTime;
  }
}


extern Heating heating;
void Temperatures:: record() {
  sumOverPeriod += getCurrent();
  periodCount++;
  if (millis() > previousRecord + 600000) {
    previousRecord = millis();
    if (periodCount > 0) {
      float avgTempOverPeriod = round(10 * sumOverPeriod / periodCount) / 10;
      File f = SD.open("TEMPERATURES.TXT", FILE_REWRITE);
      if (f) {
        f.println(timeString() + " " + (heating.isHeatingOn ? 1 : 0) + " " + avgTempOverPeriod);
        f.close();
      }
      periodCount = 0;
      sumOverPeriod = 0;
    }
  }
}
