// funny_bubbles.cpp — formerly the only "bubbles" pattern, kept for its goofy
// bouncing-balls vibe.
//
// Bouncing balls with elastic, equal-mass collisions (velocity components
// along the contact normal are exchanged). Render path: erase prevX/prevY
// from last frame → step physics → draw new positions with a tiny shine pixel.
// No fillScreen — only the changed pixels are touched per frame.
//
// All balls share the active COLOR_ACCENT instead of the old cyan/blue palette
// so the screensaver follows the WHITE/YELLOW/LEGACY theme. Per-ball variety
// now comes from radius alone (5..14 px — bumped to match the chunkier look
// elsewhere) which still reads as "lots of different bubbles" without
// breaking the monochrome look.

#include "../config.h"
#include "internal.h"
#include "helpers.h"
#include <ILI9341_t3n.h>
#include <math.h>

extern ILI9341_t3n tft;

struct Bubble {
  int16_t prevX, prevY;
  float   x, y;
  float   vx, vy;
  uint8_t r;
};

// Density bumped from 12 → 20. Collision pass is N², so this is now 190 pair
// checks per frame — still trivial on Teensy 4.1.
static const int N_FUNNY = 20;
static Bubble bubbles[N_FUNNY];

static void initBubble(Bubble &b) {
  // Radius 5..14 (was 3..10) — bigger balls for a chunkier look. The N²
  // collision pass scales with count, not size, so this is free.
  b.r  = (uint8_t)randInt(5, 14);
  b.x  = randFloat(b.r + 2, SCREEN_W - b.r - 2);
  b.y  = randFloat(b.r + 2, SCREEN_H - b.r - 2);
  b.vx = randFloat(-1.6f, 1.6f);
  b.vy = randFloat(-1.6f, 1.6f);
  // Avoid duds that barely move.
  if (fabsf(b.vx) < 0.4f) b.vx = (b.vx < 0 ? -0.7f : 0.7f);
  if (fabsf(b.vy) < 0.4f) b.vy = (b.vy < 0 ? -0.7f : 0.7f);
  b.prevX = (int16_t)b.x;
  b.prevY = (int16_t)b.y;
}

void initFunnyBubbles() {
  for (int i = 0; i < N_FUNNY; i++) initBubble(bubbles[i]);
}

void stepFunnyBubbles() {
  uint16_t accent = COLOR_ACCENT;
  uint16_t bg     = COLOR_BG;
  uint16_t shine  = COLOR_FG;

  // 1. Erase old positions.
  for (int i = 0; i < N_FUNNY; i++) {
    Bubble &b = bubbles[i];
    tft.fillCircle(b.prevX, b.prevY, b.r, bg);
  }

  // 2. Move + bounce off walls.
  for (int i = 0; i < N_FUNNY; i++) {
    Bubble &b = bubbles[i];
    b.x += b.vx;
    b.y += b.vy;
    if (b.x < b.r)            { b.x = b.r;            b.vx = -b.vx; }
    if (b.x > SCREEN_W - b.r) { b.x = SCREEN_W - b.r; b.vx = -b.vx; }
    if (b.y < b.r)            { b.y = b.r;            b.vy = -b.vy; }
    if (b.y > SCREEN_H - b.r) { b.y = SCREEN_H - b.r; b.vy = -b.vy; }
  }

  // 3. Ball-ball collisions (elastic, equal mass) — N² but N is small.
  for (int i = 0; i < N_FUNNY; i++) {
    for (int j = i + 1; j < N_FUNNY; j++) {
      Bubble &a = bubbles[i];
      Bubble &c = bubbles[j];
      float dx = c.x - a.x;
      float dy = c.y - a.y;
      float minDist = (float)(a.r + c.r);
      float distSq = dx * dx + dy * dy;
      if (distSq < minDist * minDist && distSq > 0.001f) {
        float dist = sqrtf(distSq);
        float nx = dx / dist;
        float ny = dy / dist;
        float overlap = minDist - dist;
        a.x -= nx * overlap * 0.5f;
        a.y -= ny * overlap * 0.5f;
        c.x += nx * overlap * 0.5f;
        c.y += ny * overlap * 0.5f;
        float v1n = a.vx * nx + a.vy * ny;
        float v2n = c.vx * nx + c.vy * ny;
        float delta = v2n - v1n;
        a.vx += delta * nx;
        a.vy += delta * ny;
        c.vx -= delta * nx;
        c.vy -= delta * ny;
      }
    }
  }

  // 4. Draw new positions with a tiny shine pixel.
  for (int i = 0; i < N_FUNNY; i++) {
    Bubble &b = bubbles[i];
    int16_t nx = (int16_t)b.x;
    int16_t ny = (int16_t)b.y;
    tft.fillCircle(nx, ny, b.r, accent);
    // Shine pixel scaled with ball size — 2 px disc on the bigger balls so
    // the highlight reads at the new chunkier radius range.
    if (b.r >= 8) {
      tft.fillCircle(nx - b.r / 3, ny - b.r / 3, 2, shine);
    } else if (b.r >= 5) {
      tft.fillCircle(nx - b.r / 3, ny - b.r / 3, 1, shine);
    }
    b.prevX = nx;
    b.prevY = ny;
  }
}
