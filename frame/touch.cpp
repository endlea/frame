#include "touch.h"
#include <Adafruit_FT6206.h>

extern Adafruit_FT6206 ts;

TouchTracker touch;

// ---- Touch filtering ----
static const uint8_t TOUCH_PRESS_FRAMES   = 2;
static const uint8_t TOUCH_RELEASE_FRAMES = 2;
static const int     TOUCH_JITTER_PX      = 3;
static const int     TOUCH_JUMP_REJECT_PX = 45;

static bool readTouchPoint(int16_t &x, int16_t &y) {
  if (ts.touched() <= 0) return false;

  TS_Point p = ts.getPoint();

  int16_t rx = p.x;
  int16_t ry = p.y;

  if (TOUCH_SWAP_XY) {
    int16_t t = rx;
    rx = ry;
    ry = t;
  }

  if (TOUCH_FLIP_X) rx = SCREEN_W - 1 - rx;
  if (TOUCH_FLIP_Y) ry = SCREEN_H - 1 - ry;

  if (rx < 0 || rx >= SCREEN_W || ry < 0 || ry >= SCREEN_H) {
    return false;
  }

  x = rx;
  y = ry;
  return true;
}

void updateTouch() {
  touch.tapped = false;
  touch.dragged = false;
  touch.dragDX = 0;
  touch.dragDY = 0;

  int16_t mx = touch.curX;
  int16_t my = touch.curY;

  bool rawPressed = readTouchPoint(mx, my);

  if (rawPressed) {
    if (touch.pressFrames < 255) touch.pressFrames++;
    touch.releaseFrames = 0;
  } else {
    if (touch.releaseFrames < 255) touch.releaseFrames++;
    touch.pressFrames = 0;
  }

  bool stablePressed  = touch.pressFrames >= TOUCH_PRESS_FRAMES;
  bool stableReleased = touch.releaseFrames >= TOUCH_RELEASE_FRAMES;

  // Touch began.
  if (stablePressed && !touch.prevActive) {
    touch.prevActive = true;

    touch.startX = mx;
    touch.startY = my;
    touch.prevX = mx;
    touch.prevY = my;
    touch.curX = mx;
    touch.curY = my;

    touch.startMs = millis();
    touch.wasDrag = false;
    return;
  }

  // Touch moved.
  if (stablePressed && touch.prevActive) {
    touch.curX = mx;
    touch.curY = my;

    int16_t dx = mx - touch.prevX;
    int16_t dy = my - touch.prevY;

    // Reject one-frame FT6206 coordinate jumps.
    if (abs(dx) > TOUCH_JUMP_REJECT_PX || abs(dy) > TOUCH_JUMP_REJECT_PX) {
      touch.prevX = mx;
      touch.prevY = my;
      return;
    }

    // Ignore tiny jitter.
    if (abs(dx) <= TOUCH_JITTER_PX) dx = 0;
    if (abs(dy) <= TOUCH_JITTER_PX) dy = 0;

    int moveX = abs(mx - touch.startX);
    int moveY = abs(my - touch.startY);
    int totalMove = max(moveX, moveY);

    if (totalMove > TAP_DRAG_THRESHOLD_PX) {
      touch.wasDrag = true;

      if (dx != 0 || dy != 0) {
        touch.dragged = true;
        touch.dragDX = dx;
        touch.dragDY = dy;
      }
    }

    touch.prevX = mx;
    touch.prevY = my;
    return;
  }

  // Touch released.
  if (stableReleased && touch.prevActive) {
    touch.prevActive = false;

    if (!touch.wasDrag && (millis() - touch.startMs) < TAP_MAX_DURATION_MS) {
      touch.tapped = true;

      // Release point is better for selecting rows in lists.
      touch.tapX = touch.startX;
      touch.tapY = touch.startY;
    }
  }
}