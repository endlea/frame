// bubbles.cpp — close ER-301 Bubbles port.
// Important ER-301 quirk: negative radius is NOT skipped.
// Their circle() with negative radius becomes a tiny 4-point star.
// That is the "circle turns into star" behavior.

#include "../config.h"
#include "internal.h"
#include "helpers.h"
#include <ILI9341_t3n.h>
#include <math.h>

extern ILI9341_t3n tft;

// ---- Tunables ----
static const int   N_BUBBLES = 36;
static const float SPAWN_PROB_PER_FRAME = 0.05f;

// ER-301 original: R = p*p*8 + 2.
// Slightly scaled for 240x320 TFT, but still small/OLED-like.
static const float MIN_BASE_R = 2.0f;
static const float MAX_BASE_R = 12.0f;

static const float RATE = 3.14f;

// ---- State ----
struct ErBubble {
  int16_t x, y;
  float   baseR;
  int8_t  prevR;    // signed radius, can be negative
};

static ErBubble erBubbles[N_BUBBLES];
static float er_t = 0.0f;

// Exact ER-301-style Bresenham circle behavior.
// Negative radius intentionally draws a 4-point star/cross.
static inline void erCirclePoints(int cx, int cy, int x, int y, uint16_t color) {
  if (x == 0) {
    tft.drawPixel(cx,     cy + y, color);
    tft.drawPixel(cx,     cy - y, color);
    tft.drawPixel(cx + y, cy,     color);
    tft.drawPixel(cx - y, cy,     color);
  } else if (x == y) {
    tft.drawPixel(cx + x, cy + y, color);
    tft.drawPixel(cx - x, cy + y, color);
    tft.drawPixel(cx + x, cy - y, color);
    tft.drawPixel(cx - x, cy - y, color);
  } else if (x < y) {
    tft.drawPixel(cx + x, cy + y, color);
    tft.drawPixel(cx - x, cy + y, color);
    tft.drawPixel(cx + x, cy - y, color);
    tft.drawPixel(cx - x, cy - y, color);
    tft.drawPixel(cx + y, cy + x, color);
    tft.drawPixel(cx - y, cy + x, color);
    tft.drawPixel(cx + y, cy - x, color);
    tft.drawPixel(cx - y, cy - x, color);
  }
}

static void erCircle(int cx, int cy, int radius, uint16_t color) {
  int x = 0;
  int y = radius;
  int p = (5 - radius * 4) / 4;

  erCirclePoints(cx, cy, x, y, color);

  while (x < y) {
    x++;
    if (p < 0) {
      p += 2 * x + 1;
    } else {
      y--;
      p += 2 * (x - y) + 1;
    }
    erCirclePoints(cx, cy, x, y, color);
  }
}

static void eraseBubble(const ErBubble &b) {
  if (b.baseR <= 0.0f) return;
  erCircle(b.x, b.y, b.prevR, COLOR_BG);
}

static void drawBubble(const ErBubble &b, int r) {
  erCircle(b.x, b.y, r, COLOR_ACCENT);
}

void initBubbles() {
  for (int i = 0; i < N_BUBBLES; i++) {
    erBubbles[i].x = 0;
    erBubbles[i].y = 0;
    erBubbles[i].baseR = 0.0f;
    erBubbles[i].prevR = 0;
  }

  er_t = 0.0f;
  tft.fillScreen(COLOR_BG);
}

void stepBubbles() {
  er_t += SAVER_FRAME_MS / 1000.0f;
  if (er_t > 24.0f * 3600.0f) er_t = 0.0f;

  if (randFloat(0.0f, 1.0f) < SPAWN_PROB_PER_FRAME) {
    float p = randFloat(0.0f, 1.0f);
    int j = (int)(p * (N_BUBBLES - 1));

    ErBubble &b = erBubbles[j];

    eraseBubble(b);

    b.x = (int16_t)randInt(0, SCREEN_W);
    b.y = (int16_t)randInt(0, SCREEN_H);

    b.baseR = p * p * (MAX_BASE_R - MIN_BASE_R) + MIN_BASE_R;
    b.prevR = 0;
  }

  for (int i = 0; i < N_BUBBLES; i++) {
    ErBubble &b = erBubbles[i];
    if (b.baseR <= 0.0f) continue;

    eraseBubble(b);

    // ER-301 original:
    // int r = R[i] * sinf(x + t * 3.14f);
    //
    // Do NOT skip negative r.
    // Negative radius is the star phase.
    int r = (int)(b.baseR * sinf((float)b.x + er_t * RATE));

    drawBubble(b, r);
    b.prevR = (int8_t)r;
  }
}