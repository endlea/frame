// touch.h — capacitive touch input + tap/drag tracker.
// Reads the FT6206, applies axis flips/swap, classifies presses as tap or drag.
#pragma once
#include <Arduino.h>
#include "config.h"

struct TouchTracker {
  // Internal state — don't poke from outside.
  bool prevActive = false;
  int16_t startX = 0, startY = 0;
  int16_t prevX = 0, prevY = 0;
  uint32_t startMs = 0;
  bool wasDrag = false;

  // Outputs of the last updateTouch() call. Read these from loop().
  bool tapped = false;          // released after a short, non-moving press
  bool dragged = false;         // movement detected this frame
  int16_t dragDX = 0, dragDY = 0;  // delta since previous frame
  int16_t tapX = 0, tapY = 0;   // position of the tap (screen coords)
};

extern TouchTracker touch;

// Call once per loop() iteration before reading touch.tapped / touch.dragged.
void updateTouch();