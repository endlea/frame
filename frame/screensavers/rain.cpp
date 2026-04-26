// rain.cpp — dense fast pixel-art diagonal rain.
// Longer, slimmer drops. Direction: RIGHT → LEFT and DOWN.

#include "../config.h"
#include "internal.h"
#include "helpers.h"
#include <ILI9341_t3n.h>

extern ILI9341_t3n tft;

// ---- Tunables ----
static const int   N_DROPS    = 120;

static const int   MIN_LEN    = 8;
static const int   MAX_LEN    = 18;

static const float MIN_SPEED_X = 2.4f;
static const float MAX_SPEED_X = 4.4f;

static const float MIN_SPEED_Y = 7.0f;
static const float MAX_SPEED_Y = 12.0f;

struct Drop {
  float   x;
  float   y;
  uint8_t length;
  float   speedX;
  float   speedY;
  uint8_t thickness;
};

static Drop drops[N_DROPS];

static inline void drawBlock(int x, int y, uint8_t size, uint16_t color) {
  if (x < -size || x >= SCREEN_W || y < -size || y >= SCREEN_H) return;
  tft.fillRect(x, y, size, size, color);
}

static uint8_t randomThickness() {
  int r = randInt(0, 100);
  if (r < 45) return 1;
  if (r < 96) return 2;
  return 3;
}

static void drawDrop(const Drop &d, uint16_t headColor, uint16_t tailColor) {
  int headX = (int)d.x;
  int headY = (int)d.y;

  for (int j = 0; j < (int)d.length; j++) {
    // Long pixel-art diagonal, not too windy.
    int px = headX + (j / 2);
    int py = headY - j;

    drawBlock(px, py, d.thickness, j == 0 ? headColor : tailColor);
  }
}

static void respawn(Drop &d, bool initial = false) {
  d.length    = (uint8_t)randInt(MIN_LEN, MAX_LEN + 1);
  d.speedX    = randFloat(MIN_SPEED_X, MAX_SPEED_X);
  d.speedY    = randFloat(MIN_SPEED_Y, MAX_SPEED_Y);
  d.thickness = randomThickness();

  if (initial) {
    d.x = (float)randInt(-40, SCREEN_W + 70);
    d.y = (float)randInt(-60, SCREEN_H + 70);
  } else {
    // Wider right/top spawn helps fill the lower-right corner too.
    if (randInt(0, 100) < 35) {
      d.x = (float)randInt(SCREEN_W - 30, SCREEN_W + 90);
      d.y = (float)randInt(-20, SCREEN_H / 2);
    } else {
      d.x = (float)randInt(-10, SCREEN_W + 80);
      d.y = (float)randInt(-100, -5);
    }
  }
}

void initRain() {
  for (int i = 0; i < N_DROPS; i++) {
    respawn(drops[i], true);
  }
}

void stepRain() {
  uint16_t bg     = COLOR_BG;
  uint16_t accent = COLOR_ACCENT;
  uint16_t dim    = (uint16_t)((accent >> 1) & 0x7BEFu);

  for (int i = 0; i < N_DROPS; i++) {
    Drop &d = drops[i];

    drawDrop(d, bg, bg);

    d.x -= d.speedX;
    d.y += d.speedY;

    if (d.x + d.length < 0 || d.y - d.length > SCREEN_H) {
      respawn(d);
      continue;
    }

    drawDrop(d, accent, dim);
  }
}