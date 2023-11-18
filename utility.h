#ifndef UF_UTILITY
#define UF_UTILITY

#include <Arduino.h>

String dayName(int i);

int dayIndex(String date);

String day (String date);
bool isSummertime();


String getProp (String &msg, String prop, int msgix, int endSegmentIx);

#endif
