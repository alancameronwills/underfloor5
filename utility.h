#ifndef UF_UTILITY
#define UF_UTILITY

#include <Arduino.h>

String dayName(int i);

int dayIndex(String date);

String day (String date);
bool isSummertime();
long rtcSeconds();
String utc (long int seconds);

String getProp (String &msg, String prop, int msgix, int endSegmentIx);

#endif
