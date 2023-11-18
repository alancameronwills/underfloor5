#ifndef UF_SCREEN
#define UF_SCREEN


#include <Arduino.h>

#include "Adafruit_GFX.h"       // Graphics https://github.com/adafruit/Adafruit-GFX-Library
#include "Adafruit_ILI9341.h"   // Screen https://github.com/adafruit/Adafruit_ILI9341/
#include "XPT2046_Touchscreen.h"
#include <Fonts/FreeSansBold9pt7b.h>


/*__Pin definitions for the TFT display */
#define TFT_CS   A3
#define TFT_DC   0
#define TFT_MOSI 8
//#define TFT_RST  22
#define TFT_CLK  9
#define TFT_MISO 10
#define BACKLIGHT  A2

#define BEEPER 2

#define TOUCH_CS A4
#define TOUCH_IRQ 1

/*____ Touchscreen parameters_____*/
#define MINPRESSURE 5      // minimum required force for touch event
#define TS_MINX 370
#define TS_MINY 470
#define TS_MAXX 3700
#define TS_MAXY 3600
/*___________________________*/


#define TIDE_COLOR 65520 // orange (red[0..31]<<11) + (green[0..63]<<5) + blue[0..31]


extern Adafruit_ILI9341 tft;
class PageController {
  public:
    virtual void setTimeout(long ms);
    virtual void switchToControlPage();
    virtual void switchToMainPage();
};
class Page {
  protected:
    PageController *screen;
    void drawNumberButton(float num, int y, unsigned int bg, unsigned int fg);

    void drawStatus ();
    unsigned rgb(byte r, byte g, byte b);
  public:
    //virtual void activateButtons();
    virtual void handleTouch(TS_Point touch) ;
    virtual void handleLeavingPage() ;
    virtual void redraw() ;  // redraw from scratch
    virtual void refresh() ; // update just the changeable bits
};
class MainPage : public Page {
    void drawTemperature();
    void drawSunMoon(int left, int base, int dx);
    void drawTides(int left, int base, int dx);
    void drawHeatingPlan();
    void drawTempBars();
    void drawWindArrow(int day, String dirn);
    void drawTempBar(int xv, float t, float rain, float wind);
    void drawTempGraphBg(int firstDay);
  public:
    MainPage(PageController *s) {
      screen = s;
    }
    void handleTouch(TS_Point touch) ;
    void handleLeavingPage() ;
    void redraw() ;
    void refresh();
};
class ControlPage : public Page {
    void (*completion)(float);
  public:
    ControlPage(PageController *s, void (*completeAction)(float)) {
      screen = s;
      completion = completeAction;
    }
    void handleTouch(TS_Point touch) ;
    void handleLeavingPage() ;
    void redraw() ;
    void refresh();
};
class StartupPage : public Page {
  public:
    StartupPage(PageController *s) {
      screen = s;
    }
    void handleTouch(TS_Point touch) {}
    void handleLeavingPage() {}
    void redraw() ;
    void refresh() {}
};
class Screen : PageController {
    XPT2046_Touchscreen touchScreen;
    long screenTimeout = 0;       // revert to main screen after millis
    unsigned lastTouch = 0;
    ControlPage *controlPage;
    MainPage *mainPage = new MainPage(this);
    StartupPage *startupPage = new StartupPage(this);
    Page *currentPage = NULL;
    bool IsTouched(TS_Point &p);
    void switchPage(Page *toPage) {
      if (toPage == currentPage) return;
      if (currentPage != NULL) currentPage->handleLeavingPage();
      currentPage = toPage;
      redraw();
    }
  public:
    Screen(void (*controlCompleteAction)(float)) : touchScreen(TOUCH_CS) {
      controlPage = new ControlPage(this, controlCompleteAction);
    }
    void start() {
      pinMode(TFT_CS, OUTPUT);    // screen chip select
      digitalWrite(TFT_CS, HIGH);
      tft.begin();
      touchScreen.begin();
      switchPage(startupPage);
    }
    void loop() {
      TS_Point touchPoint;
      if (IsTouched(/*out*/touchPoint)) {
        currentPage->handleTouch(touchPoint);
      }
      if (screenTimeout > 0 && millis() > screenTimeout) {
        screenTimeout = 0;
        switchPage(mainPage);
      }
    }
    void setTimeout(long ms) {
      if (ms == 0) screenTimeout = 0;
      else screenTimeout = millis() + ms;
    }
    void switchToMainPage() {
      switchPage(mainPage);
    }
    void switchToControlPage() {
      switchPage(controlPage);
    }
    void redraw() {
      currentPage->redraw();
    }
    void refresh() {
      currentPage->refresh();
    }
};


void show(int x, int y, String text);

void backlightOn(bool on) ;

/// Draw vertical gradient across width of screen. 5-6-5 color: r:{0..31},g:{0..63},b:{0..31}
void vgrade(float rFrom, float gFrom, float bFrom, float rTo, float gTo, float bTo, int yFrom, int yTo);


unsigned rgb(byte r, byte g, byte b);



void drawTempBars() ;
void drawWindArrow(int day, String dirn);
void drawTempBar(int xv, float t, float rain, float wind);
void drawTempGraphBg(int firstDay);


void showStatus(String s);

#endif
