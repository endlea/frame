// stars.cpp — twinkling stationary sparkles, single-colour.
//
// Each star is a 5-pixel "plus" shape (centre + 4 cardinals):
//
//      .X.
//      XXX
//      .X.
//
// That reads more like a pointy spark than a single dot, and matches the
// "крупнее (шире рисовка)" direction — the rest of the UI moved to chunky
// pixel-art elsewhere, stars follow suit.
//
// Brightness oscillates between 0 and `base`; the colour is the active
// COLOR_ACCENT scaled by phase, so the whole field follows the theme
// (white in WHITE, amber in YELLOW, cyan in LEGACY) without any per-star
// colour bookkeeping.
//
// We don't erase: the plus shape is the same every frame, only its colour
// changes, so overwriting in place is enough.

#include "../config.h"
#include "internal.h"
#include "helpers.h"
#include <ILI9341_t3n.h>

extern ILI9341_t3n tft;

struct Star {
  int16_t x, y;
  uint8_t base;     // peak brightness scale (10..32)
  uint8_t phase;    // current 0..base
  int8_t  step;     // +2 or -2
};

// Density. Slightly lower than the old single-pixel version (110) because
// each star now paints 5 pixels — the visual mass per spark is much higher
// so the field still feels dense without becoming a wall of pixels.
static const int N_STARS = 90;
static Star stars[N_STARS];

// Paint a 5-pixel plus centred at (x,y) in `color`.
static inline void plusPixel(int16_t x, int16_t y, uint16_t color) {
  if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) tft.drawPixel(x, y, color);
  if (x > 0)              tft.drawPixel(x - 1, y, color);
  if (x < SCREEN_W - 1)   tft.drawPixel(x + 1, y, color);
  if (y > 0)              tft.drawPixel(x, y - 1, color);
  if (y < SCREEN_H - 1)   tft.drawPixel(x, y + 1, color);
}

void initStars() {
  for (int i = 0; i < N_STARS; i++) {
    // Inset by 1 px so the plus arms never spill off the edge.
    stars[i].x    = (int16_t)randInt(1, SCREEN_W - 1);
    stars[i].y    = (int16_t)randInt(1, SCREEN_H - 1);
    stars[i].base = (uint8_t)randInt(10, 32);
    stars[i].phase = (uint8_t)randInt(0, stars[i].base);
    stars[i].step = (random(2) == 0) ? -2 : 2;
  }
}

void stepStars() {
  // Unpack the accent colour once per frame so the per-star inner loop only
  // does multiplies and shifts.
  uint16_t acc   = COLOR_ACCENT;
  uint8_t  accR5 = (acc >> 11) & 0x1F;
  uint8_t  accG6 = (acc >>  5) & 0x3F;
  uint8_t  accB5 = (acc      ) & 0x1F;

  for (int i = 0; i < N_STARS; i++) {
    Star &s = stars[i];

    int p = (int)s.phase + s.step;
    if (p < 0)         { p = 0;       s.step =  2; }
    if (p > s.base)    { p = s.base;  s.step = -2; }
    s.phase = (uint8_t)p;

    // Scale the accent's RGB565 channels by phase/base. This produces a pure
    // tint of the active theme — no warm/cool mix, no hint of any other hue.
    uint8_t r5 = (uint8_t)((accR5 * s.phase) / s.base);
    uint8_t g6 = (uint8_t)((accG6 * s.phase) / s.base);
    uint8_t b5 = (uint8_t)((accB5 * s.phase) / s.base);

    uint16_t color = (uint16_t(r5) << 11) | (uint16_t(g6) << 5) | uint16_t(b5);
    plusPixel(s.x, s.y, color);
  }
}
