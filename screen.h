#ifndef UF_SCREEN
#define UF_SCREEN


#include <Arduino.h>
#include "Adafruit_ILI9341.h"   // Screen https://github.com/adafruit/Adafruit_ILI9341/
#include "XPT2046_Touchscreen.h"



void showIP();
void showStatus(String s);

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
    bool scheduledRefresh = false;
    void switchPage(Page *toPage) ;
  public:
    Screen(void (*controlCompleteAction)(float));
    void start() ;
    void loop() ;
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
      scheduledRefresh = false;
      currentPage->refresh();
    }
    void scheduleRefresh() {
      scheduledRefresh = true;
    }
};

class Backlight {
    const unsigned long backlightTimeout = 120 * 1000L; // ms
    const int backlightSensitivity = 12; // 3..20
    bool isBacklightOn = true;      // Display backlight is lit, will time out
    unsigned long backlightWentOn = 0;
    int recentLux = 0;
    int skip = 0;
    int blinker = 0;
  public:
    void setup() ;
    void loop(unsigned long m) ;
    void on(bool on);
};

#endif
