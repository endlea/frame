// screensaver.h — public API for the menu's animated background.
//
// Implementations live in /screensavers/ at the sketch root. Each pattern is
// its own .cpp file in that folder; dispatcher.cpp wires them together via
// the SaverPattern enum.
//
// Build note: Arduino IDE doesn't auto-compile arbitrary subfolders (only the
// root and src/), so /screensavers/*.cpp is pulled in via the root-level
// screensavers.cpp wrapper which #includes them all. See that file for the
// implications (shared TU → no duplicate file-scope static names).
//
// Adding a new pattern:
//   1. Add an enum value below (before SAVER_COUNT)
//   2. Add init/step declarations in /screensavers/internal.h
//   3. Implement them in /screensavers/<your_pattern>.cpp. Headers in the
//      sketch root must be reached with `"../config.h"` etc. — the Arduino IDE
//      include-path search doesn't always cover the parent directory from a
//      non-src subfolder.
//   4. Wire the new value into dispatcher.cpp (saverName / saverInit / saverStep)
//   5. Add `#include "screensavers/<your_pattern>.cpp"` to root screensavers.cpp
#pragma once
#include <Arduino.h>

enum SaverPattern : uint8_t {
  SAVER_NONE,            // off — black background
  SAVER_BUBBLES,         // ER-301 style: pulsing circles
  SAVER_RAIN,            // pixel-art falling rain (replaces the old LINES port)
  SAVER_STARS,           // twinkling stationary stars
  SAVER_FUNNY_BUBBLES,   // bouncing balls with elastic collisions
  SAVER_BURST,           // pixelated exploding stars
  SAVER_FRACTALS,        // smooth Mandelbrot fractal
  SAVER_COUNT            // keep last; equals number of patterns including OFF
};

extern SaverPattern currentSaver;

// Human-readable name shown in the Options screen.
const char *saverName(SaverPattern p);

// Reset particle state for the chosen pattern. Called from setup() once at
// boot, and from the Options screen when the user picks a different pattern.
// NOT called from enterMenu() / idle / wake — the saver's state is meant to
// persist across those transitions so the chrome appears to "melt away" on
// idle and re-appear on wake without resetting the background.
void saverInit(SaverPattern p);

// Advance the current pattern by one frame. Caller is responsible for
// throttling (SAVER_FRAME_MS in config.h) and for calling tft.updateScreen().
void saverStep();
