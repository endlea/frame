// dispatcher.cpp — owns currentSaver and routes the active SaverPattern enum
// to the right init/step pair. Lives in /screensavers/ alongside each
// pattern's implementation.
//
// Arduino IDE only auto-compiles .cpp at the sketch root or under src/, so
// /screensavers/*.cpp is NOT picked up directly. Instead, the root-level
// screensavers.cpp #includes every file in this folder, making them part of
// one translation unit. See screensavers.cpp for the why.
#include "../screensaver.h"
#include "internal.h"

// User's currently selected pattern. Persists across enterMenu() calls.
// Initial value chosen here = the boot default. (Anything except SAVER_COUNT
// is fine; SAVER_BUBBLES is the new ER-301-style pulser.)
SaverPattern currentSaver = SAVER_BUBBLES;

const char *saverName(SaverPattern p) {
  switch (p) {
    case SAVER_NONE:          return "OFF";
    case SAVER_BUBBLES:       return "BUBBLES";
    case SAVER_RAIN:          return "RAIN";
    case SAVER_STARS:         return "STARS";
    case SAVER_FUNNY_BUBBLES: return "FUNNY BUBBLES";
    case SAVER_BURST:         return "BURST";
    case SAVER_FRACTALS:      return "FRACTALS";
    default:                  return "?";
  }
}

void saverInit(SaverPattern p) {
  currentSaver = p;
  switch (p) {
    case SAVER_BUBBLES:       initBubbles();       break;
    case SAVER_RAIN:          initRain();          break;
    case SAVER_STARS:         initStars();         break;
    case SAVER_FUNNY_BUBBLES: initFunnyBubbles();  break;
    case SAVER_BURST:         initBurst();         break;
    case SAVER_FRACTALS:      initFractals();      break;
    default: break;   // SAVER_NONE: nothing to init
  }
}

void saverStep() {
  switch (currentSaver) {
    case SAVER_BUBBLES:       stepBubbles();       break;
    case SAVER_RAIN:          stepRain();          break;
    case SAVER_STARS:         stepStars();         break;
    case SAVER_FUNNY_BUBBLES: stepFunnyBubbles();  break;
    case SAVER_BURST:         stepBurst();         break;
    case SAVER_FRACTALS:      stepFractals();      break;
    default: break;
  }
}
