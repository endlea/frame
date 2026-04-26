#include "touch.h"
#include <Adafruit_FT6206.h>

// `ts` is defined in gif_player_ui.ino (the FT6206 driver instance).
extern Adafruit_FT6206 ts;

TouchTracker touch;

void updateTouch() {
  touch.tapped = false;
  touch.dragged = false;
  touch.dragDX = touch.dragDY = 0;

  bool isPressed = ts.touched() > 0;
  int16_t mx = touch.prevX, my = touch.prevY;

  if (isPressed) {
    TS_Point p = ts.getPoint();
    int16_t rx = p.x;
    int16_t ry = p.y;
    if (TOUCH_SWAP_XY) { int16_t t = rx; rx = ry; ry = t; }
    if (TOUCH_FLIP_X) rx = SCREEN_W - 1 - rx;
    if (TOUCH_FLIP_Y) ry = SCREEN_H - 1 - ry;
    mx = rx; my = ry;
  }

  if (isPressed && !touch.prevActive) {
    // Touch began
    touch.startX = mx;
    touch.startY = my;
    touch.prevX = mx;
    touch.prevY = my;
    touch.startMs = millis();
    touch.wasDrag = false;
  } else if (isPressed && touch.prevActive) {
    int16_t dx = mx - touch.prevX;
    int16_t dy = my - touch.prevY;
    int totalDist = abs(mx - touch.startX) + abs(my - touch.startY);
    if (totalDist > TAP_DRAG_THRESHOLD_PX) {
      touch.wasDrag = true;
      touch.dragged = true;
      touch.dragDX = dx;
      touch.dragDY = dy;
    }
    touch.prevX = mx;
    touch.prevY = my;
  } else if (!isPressed && touch.prevActive) {
    // Touch released — emit a tap if it was short and didn't move
    if (!touch.wasDrag && (millis() - touch.startMs) < TAP_MAX_DURATION_MS) {
      touch.tapped = true;
      touch.tapX = touch.prevX;
      touch.tapY = touch.prevY;
    }
  }

  touch.prevActive = isPressed;
}