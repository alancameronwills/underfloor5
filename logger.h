#ifndef UF_logger
#define UF_logger

#include <Arduino.h>

#include <SPI.h>
#include <SD.h>         // SD card for log and param files
#include <RTCZero.h>    // clock

// SD card
#define SD_CS   4 //   SDCARD_SS_PIN   // SD card chip select pin
#define FILE_REWRITE (O_WRITE | O_CREAT | O_TRUNC)


extern RTCZero rtc;

void copyFile(File fi, String toFile, bool overWrite);
void copyFile(String fromFile, String toFile, bool overWrite);

void transferRecentLog();

void clearRecentLog ();

void truncateLog(const String& logfile);

String timeString ();
String d2(int n);
String TwoDigits(String n);
String ThreeChars(String n);
void clearFile(String fileName) ;


void clog (String msg);
void dlogn(String msg) ;
void clogn(String msg);
void dclogn(String msg, bool fileLog);

void rlog (String msg, const char* fileName) ;




#endif
