#ifndef UF_logger
#define UF_logger

#include <Arduino.h>
#include <SD.h>  

#define FILE_REWRITE (O_WRITE | O_CREAT | O_TRUNC)



void sd_logger_start();

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
