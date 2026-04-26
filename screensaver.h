// screensaver.h — generative animated background for the main menu.
//
// One pattern is active at a time. Patterns draw "behind" the menu chrome:
// the main loop calls saverStep() each frame, then redraws the static chrome
// on top, so anything in the chrome rectangles always wins.
//
// Adding a new pattern:
//   1. add an enum value before SAVER_COUNT
//   2. write static initFoo() and stepFoo() in screensaver.cpp
//   3. add cases in saverName() / saverInit() / saverStep()
#pragma once
#include <Arduino.h>

enum SaverPattern : uint8_t {
  SAVER_NONE,
  SAVER_BUBBLES,
  SAVER_STARS,
  SAVER_SNOW,
  SAVER_COUNT       // keep last; equals the number of patterns
};

extern SaverPattern currentSaver;

const char *saverName(SaverPattern p);
void saverInit(SaverPattern p);   // (re)set particle state and switch active pattern
void saverStep();                 // advance one frame; no-op if SAVER_NONE