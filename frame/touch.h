#pragma once
#include <Arduino.h>
#include "config.h"

struct TouchTracker {
  bool prevActive = false;

  int16_t startX = 0, startY = 0;
  int16_t prevX = 0, prevY = 0;
  int16_t curX = 0, curY = 0;

  uint32_t startMs = 0;
  bool wasDrag = false;

  uint8_t pressFrames = 0;
  uint8_t releaseFrames = 0;

  bool tapped = false;
  bool dragged = false;
  int16_t dragDX = 0, dragDY = 0;
  int16_t tapX = 0, tapY = 0;
};

extern TouchTracker touch;

void updateTouch();