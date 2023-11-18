#include "utility.h"
#include "screen.h"
#include "logger.h"
#include "outside.h"
#include "inside.h"



extern Tidal tidal;
extern Weather weather;
extern Temperatures temperatures;
extern Heating heating;

extern float targetTemp;

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

bool Screen::IsTouched(TS_Point &p) {
  // Crude debounce:
  unsigned nowMillis = millis();
  if (nowMillis - lastTouch < 500) return false;
  lastTouch = nowMillis;

  p = touchScreen.getPoint();
  delay(1);

  p.x = map(p.x, TS_MINX, TS_MAXX, 320, 0);
  p.y = map(p.y, TS_MINY, TS_MAXY, 240, 0);

  if (p.z < MINPRESSURE) p.z = 0;
  else {
    clogn(String("x ") + p.x + " y " + p.y);
    tone(BEEPER, 1000, 500);
    return true;
  }
  return false;
}

/*
   Screen
*/

const unsigned int bgcolor = 8;
const int leftAxis = 30;
const int dayWidth = 42;
const int zeroAxis = 90;
const int barWidth = 8;



void Page::drawNumberButton(float num, int y, unsigned int bg, unsigned int fg)
{
  tft.fillRoundRect(240, y, 80, 60, 4, bg);
  tft.setTextSize(2);
  tft.setTextColor(fg);
  tft.setFont(&FreeSansBold9pt7b);
  float t = round(10 * num) / 10.0 + 0.00001;
  show (245, y + 40, (String("") + t).substring(0, 4));
  tft.setFont(NULL);
}


unsigned Page::rgb(byte r, byte g, byte b)
{
  // (red[0..31]<<11) + (green[0..63]<<5) + blue[0..31]
  return ((r & 248) << 8) + ((g & 252) << 3) + ((b & 248) >> 3);
}


void MainPage::drawHeatingPlan()
{
  const int left = 12, base = 188, dx = 9;
  unsigned int color = 66757;
  tft.fillRect(left, 128, 240 - left, base - 128, bgcolor);
  if (heating.serviceOff || heating.serviceOn != 0) {
    tft.setTextColor(31 << 11 | 63 << 5 | 31);
    show(14, 140, heating.serviceOff ? String("SERVICE OFF") : String("SERVICE ON ") + (heating.serviceOn - millis()) / 60000);
    return;
  }

  drawSunMoon(left, base, dx);

  // Graph of minutes in each hour:
  int x = left;
  for (int i = 0; i < 24; i++) {
    int h = heating.period[i];
    tft.fillRect(x, base - h, dx - 1, h + 2, color);
    x += dx;
  }
  // 10-minute graduations:
  for (int m = 0; m < 60; m += 10) {
    tft.drawFastHLine(left, base - m, 240 - left, bgcolor);
  }
  // x-axis hour labels:
  for (int h = 0; h < 24 ; h += 6) {
    tft.setTextSize(2);
    tft.setTextColor(color);
    show (left + h * dx, 192, String(h));
  }
  // current time:
  tft.drawFastVLine((int)((rtc.getHours() + rtc.getMinutes() / 60.0)*dx) + left, 128, 60, TIDE_COLOR);

  if (heating.lowUntilDate.length() > 0) {
    tft.setTextColor(31 << 11);
    show(left + 10, 140, "Vacation to");
    show(left + 10, 160, heating.lowUntilDate);
  }

  drawTides(left, base, dx);
}

void MainPage::drawSunMoon(int left, int base, int dx) {
  const unsigned int dayColor = 20 << 11 | 32 << 5 | 8;
  const unsigned int moonColor = 8 << 11 | 16 << 5 | 8;
  //clogn(String ("Sun ") + tidal.sunRise + ".." + tidal.sunSet + " Moon " + tidal.moonRise + ".." + tidal.moonSet);
  if (tidal.sunRise < 1.0 || tidal.sunRise > 10.0 || tidal.sunSet < 15.0 || tidal.sunSet > 23.0) return;
  if (tidal.moonRise > 0 || tidal.moonSet > 0) {
    if (tidal.moonRise < tidal.moonSet) {
      tft.fillRect((int)(left + tidal.moonRise * dx), 128, (int)((tidal.moonSet - tidal.moonRise)*dx), base - 128, moonColor);
    } else {
      tft.fillRect((int)(left + tidal.moonRise * dx), 128, (int)((24 - tidal.moonRise)*dx), base - 128, moonColor);
      tft.fillRect(left, 128, (int)(tidal.moonSet * dx), base - 128, moonColor);
    }
  }
  tft.fillRect((int)(left + tidal.sunRise * dx), 128, (int)((tidal.sunSet - tidal.sunRise)*dx), base - 128, dayColor);
}

void MainPage::drawTides(int left, int base, int dx)
{
  int top = 0, bottom = 0, x0 = 0, y0 = 0;
  for (int i = 0; i < 4; i++)
  {
    Tide &t = tidal.tides[i];
    if (t.eventType.length() == 0) break;
    int x = left + t.tod * dx;
    int y = base - t.height * (base - 128) / 10;
    if (x0 == 0) {
      x0 = x - left;
      y0 = y;
    }
    //clogn(String("drawTide ") + x + ", " + y);
    tft.drawFastHLine(x - 4, y, 8, TIDE_COLOR);
    tft.drawFastVLine(x, y - (t.eventType[0] == '1' ? 8 : 0), 8, TIDE_COLOR);
    if (t.eventType[0] == '1') bottom = y;
    else top = y;
  }
  if (y0 > 0) {
    // Draw a sine wave through the low and high tides.
    // Start at the latest high or low, then wrap around.
    const float tidalPeriod = 12.42; // hours
    const float periodInPixels = dx * tidalPeriod; // about 112
    const float ddy = 2 * 3.14159 / periodInPixels; // about 0.0562
    float mid = (bottom + top) / 2;
    float yh = y0 - mid, yv = 0; // height, velocity
    int width = dx * 24;
    for (int t = 0; t < width; t++) {
      tft.drawPixel((t + x0) % width + left, int(yh + mid), TIDE_COLOR);
      yv -= yh * ddy; yh += yv * ddy;
    }
  }
}

void show(int x, int y, String text)
{
  tft.setCursor(x, y);
  tft.print(text);
}

void backlightOn(bool on) {
  digitalWrite(BACKLIGHT, on ? LOW : HIGH);
}

/// Draw vertical gradient across width of screen. 5-6-5 color: r:{0..31},g:{0..63},b:{0..31}
void vgrade(float rFrom, float gFrom, float bFrom, float rTo, float gTo, float bTo, int yFrom, int yTo)
{
  int h = yTo - yFrom;
  float r = rFrom, g = gFrom, b = bFrom;
  float dr = (rTo - rFrom) / h, dg = (gTo - gFrom) / h, db = (bTo - bFrom) / h;

  for (int y = yFrom; y < yTo; y++) {
    unsigned int color = int(r) << 11 | int(g) << 5 | int(b);
    tft.drawFastHLine(0, y, tft.width(), color);
    r += dr; g += dg; b += db;
  }
}



/***** MainPage ****/

void MainPage::handleTouch(TS_Point touch) {
  if (touch.x < 200) return;
  screen->switchToControlPage(); // controls
}
void MainPage::redraw() {
  tft.fillScreen(bgcolor);

  if (weather.forecast[0].fcDate.length() > 0) {
    drawTempGraphBg(dayIndex(weather.forecast[0].fcDate));
    drawTempBars();
  }
  this->refresh();
  screen->setTimeout(0);
}

void MainPage::handleLeavingPage() {
  screen->setTimeout(20000);
}


void MainPage::refresh() {
  drawHeatingPlan();
  drawStatus();
  drawTemperature();
}


void MainPage::drawTemperature() {
  drawNumberButton(temperatures.getCurrent(), 130, rgb(0, 192, 192), rgb(0, 0, 128));
  drawNumberButton(targetTemp, 50, rgb(0, 0, 128), rgb(0, 192, 192));
}


void MainPage::drawTempBars() {
  for (int i = 0; i < 5; i++)
  {
    WeatherDay& fc = weather.forecast[i];
    if (fc.fcDate.length() > 0) {
      drawTempBar(i * 2, fc.tempMax.toFloat(), fc.precip.toFloat(), fc.windSpeed.toFloat());
      drawTempBar(i * 2 + 1, fc.tempMin.toFloat(), fc.precipN.toFloat(), fc.windSpeed.toFloat());
      drawWindArrow(i, fc.windDirection);
    }
  }
}

void MainPage::drawWindArrow(int day, String dirn) {
  const unsigned int lineColor = rgb(0, 255, 0);
  int dx = 0, dy = 0;
  const char* c = dirn.c_str();
  if (c[0] == 'S' || c[0] == 'N') {
    dy = c[0] == 'N' ? -10 : 10;
    if (c[1] == '\0') dx = 0;
    else  {
      if (c[1] == 'W') dx = -10;
      else if (c[1] == 'E') dx = 10;
      else if (c[2] == 'W') dx = -3;
      else dx = 3;
    }
  } else {
    dx = c[0] == 'W' ? -10 : 10;
    if (c[1] == 'S') dy = 3;
    else if (c[1] == 'N') dy = -3;
  }
  int y = 15;
  int x = leftAxis + 15 + day * dayWidth;
  tft.fillRect(x - 1, y - 1, 3, 3, lineColor);
  tft.drawLine(x, y, x - dx, y - dy, lineColor);
}

void MainPage::drawTempBar(int xv, float t, float rain, float wind)
{
  //clogn (String("tempBar ") + t + " " + rain);
  int width = min(18, 4 + wind / 3);
  int x = leftAxis + 10 + (xv) * dayWidth / 2 - width / 2;
  int h = int(min(30, max(-10, t)) * 3);
  unsigned int barColor = int((100 - rain) * 0.63) << 5;
  if (h < 2 && h > -2)
    tft.fillRect(x, zeroAxis - 3, width, 6, barColor);
  else if (h > 0)
    tft.fillRect(x, zeroAxis - h, width, h, barColor);
  else
    tft.fillRect(x, zeroAxis, width, -h, barColor);
}

void MainPage::drawTempGraphBg(int firstDay)
{
  vgrade(26, 16, 4, 32, 54, 12, 0, 75);
  vgrade(32, 54, 12, 26, 62, 29, 75, 90);
  vgrade(26, 62, 29, 0, 63, 31, 90, 120);
  // X axis:
  tft.drawFastHLine(leftAxis, 90, tft.width() - 20, 0);
  // Graduations at 10 and 20:
  unsigned int grey = 16 << 11 | 32 << 5 | 16;
  tft.drawFastHLine(leftAxis, 15, tft.width() - 20, grey);
  tft.drawFastHLine(leftAxis, 30, tft.width() - 20, grey);
  tft.drawFastHLine(leftAxis, 45, tft.width() - 20, grey);
  tft.drawFastHLine(leftAxis, 60, tft.width() - 20, grey);
  tft.drawFastHLine(leftAxis, 75, tft.width() - 20, grey);
  tft.drawFastHLine(leftAxis, 85, tft.width() - 20, grey);
  // Y axis:
  tft.drawFastVLine(leftAxis, 0, 120, 0);
  // X axis labels:
  tft.setTextColor(0);
  tft.setTextSize(2); // 12 x 16
  show(4, 22, "20");
  show(4, 52, "10");
  show(4, 82, " 0");
  // Y axis labels:
  int dayCursor = 30;
  for (int i = firstDay; i < firstDay + 5; i++) {
    show(dayCursor, 92, dayName(i % 7));
    dayCursor += dayWidth;
  }
}


/***** ControlPage ****/

void ControlPage::handleLeavingPage() {
  completion(targetTemp);
}

void ControlPage::handleTouch(TS_Point touch) {
  if (touch.x > 200) {
    screen->switchToMainPage();
  } else {
    if (touch.y < 120) targetTemp += 0.5;
    else targetTemp -= 0.5;
    this->refresh();
    screen -> setTimeout(20000);
  }
}
void ControlPage::redraw() {
  tft.fillScreen(rgb(255, 255, 0));
  drawNumberButton(targetTemp, 50, rgb(0, 0, 128), rgb(0, 192, 192));
  tft.fillTriangle(100, 20, 170, 100, 30, 100, rgb(255, 192, 192));
  tft.fillTriangle(100, 220, 170, 140, 30, 140, rgb(192, 255, 255));
}
void ControlPage::refresh() {
  drawNumberButton(targetTemp, 50, rgb(0, 0, 128), rgb(0, 192, 192));
}

/******************/

void StartupPage::redraw() {
  tft.setRotation(1);
  tft.fillScreen(rgb(0, 60, 64)); // (15 << 5) + 8);
  tft.fillCircle(120, 120, 60, rgb(0, 255, 0)); // 31 << 11); // rgb(255,0,0)
  tft.setTextColor(0);
  tft.setTextSize(2); // 12 x 16
}

/*******************/


void showStatus(String s) {
  tft.fillRect(0, 220, 240, 20, 0xFFFF);
  tft.setTextSize(2);
  tft.setTextColor(16);
  show (4, 222, s);
}


/***** Screen **************/
