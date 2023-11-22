#include "logger.h"

#include <SPI.h>
#include <SD.h>         // SD card for log and param files
#include <RTCZero.h>    // clock

extern bool logging;
unsigned long maxLogSize = 1000000;
extern RTCZero rtc;

#define SD_CS   4 //   SDCARD_SS_PIN   // SD card chip select pin

void copyFile(File fi, String toFile, bool overWrite) {
  int buffersize = 3000;
  char buf[buffersize];
  File fo = SD.open(toFile, overWrite ? FILE_REWRITE : FILE_WRITE);
  if (fi && fo) {
    while (fi.available()) {
      int count = fi.read(buf, buffersize);
      if (count == 0) break;
      fo.write(buf, count);
    }
  }
  if (fi) fi.close();
  if (fo) fo.close();
}
void copyFile(String fromFile, String toFile, bool overWrite) {
  File fi = SD.open(fromFile, FILE_READ);
  copyFile(fi, toFile, overWrite);
}

void transferRecentLog() {
  copyFile("RECENT.TXT", "LOG.TXT", false);
}

void clearRecentLog () {
  clearFile("RECENT.TXT");
}

void truncateLog(const String& logfile) {
  File fi = SD.open(logfile, FILE_READ);
  if (fi) {
    long excessLength = fi.size() - maxLogSize;
    if (excessLength > 0) {
      fi.seek(excessLength);
      copyFile(fi, "OLDLOG.TXT", true);
      copyFile("OLDLOG.TXT", logfile, true);
      SD.remove("OLDLOG.TXT");
    } else {
      fi.close();
    }
  }
}


String clearFile(String fileName) {
  File f = SD.open(fileName, FILE_REWRITE);
  if (f) {
    f.println("");
    f.close();
    return String ("Emptied ") + fileName;
  } else {
    return String ("Couldn't open ") + fileName;
  }
}
String d2(int n)
{
  String r = "";
  if (n < 10) r = "0";
  r += n;
  return r;
}
String TwoDigits(String n)
{
  String r = "";
  if (n.length() < 2) r = " ";
  r += n;
  return r;
}
String ThreeChars(String n)
{
  String r = n;
  while (r.length() < 3) r += " ";
  return r;
}
String timeString () {
  return
    String("") + rtc.getYear() + "-" + d2(rtc.getMonth()) + "-" + d2(rtc.getDay())
    + " " + d2(rtc.getHours()) + ":" + d2(rtc.getMinutes()) + ":" + d2(rtc.getSeconds()) + " ";
}



void clog (String msg)
{
  dclogn(msg, false);
}
void dlogn(String msg) {
  dclogn(msg + "\n", true);
}
void clogn(String msg) {
  dclogn(msg + "\n", false);
}
bool endedLogLine = true;
void dclogn(String msg, bool fileLog)
{
  if (msg.length() == 0) return;
  String msgOut = endedLogLine ? timeString() + " " + msg : msg;
  if (logging)Serial.print(msgOut);
  if (fileLog) rlog(msgOut, "LOG.TXT");
  rlog(msgOut, "RECENT.TXT");
  endedLogLine = msgOut.charAt(msgOut.length() - 1) == '\n';
}

void rlog (String msg, const char* fileName) {
  File ff = SD.open(fileName, FILE_WRITE);
  if (ff) {
    ff.print(msg);
    ff.close();
  }
}



void sd_logger_start() {  
  pinMode(6, OUTPUT); // LED
  digitalWrite(6, LOW);
  pinMode(SD_CS, OUTPUT);       // SD card chip select
  digitalWrite(SD_CS, HIGH);
  int sdOK = SD.begin(SD_CS);
  if (logging) {
    Serial.begin(115200);
    int count = 0;
    while (!Serial) {
      // wait for serial port to connect.
      delay (100);
      if (count++ > 20) {
        logging = false;
        break;
      }
    }
  } else {
    delay(500);
  }

  dlogn(String("=====Restart====="));
  if (!sdOK) dlogn("No SD card");

  transferRecentLog();
}
