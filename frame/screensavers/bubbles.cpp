// bubbles.cpp — port of ER-301's Bubbles screensaver (Bubbles.cpp/.h in
// od/graphics/screensavers/). On the original it's a 256x64 OLED; here we run
// portrait 240x320 in COLOR_ACCENT (cyan) on COLOR_BG (black).
//
// The trick: each "bubble" sits at a fixed (x,y) once spawned. Its drawn
// radius is `baseR * sin(x*PHASE_K + t * RATE)` — so the radius oscillates
// between -baseR and +baseR over time, and we only draw when r > 0. Different
// bubbles pulse out of phase because the seed for sin includes their x
// coordinate.
//
// Differences from the literal ER-301 port:
//   - We track prevR per bubble so the previous frame's outline can be erased
//     (ER-301 OLED clears the buffer every frame; we don't).
//   - Slightly fewer bubbles (24 vs 36) to keep per-frame work in budget.
//   - Faster RATE so the pulse reads quicker, matching the user's request to
//     speed up animation.

// Note: this file is part of the unity-include built from root screensavers.cpp.
// "../config.h" walks up to the sketch root because Arduino IDE's include-path
// search doesn't always cover the parent directory from a non-src subfolder.
#include "../config.h"
#include "internal.h"
#include "helpers.h"
#include <ILI9341_t3n.h>
#include <math.h>

extern ILI9341_t3n tft;

// ---- Tunables ----
// Density bumped from 24 → 40 and spawn rate from 0.06 → 0.10 so the field
// fills out faster after init and stays visually busier.
static const int   N_BUBBLES = 40;
static const float SPAWN_PROB_PER_FRAME = 0.10f;
// Radius range bumped (3..18 → 6..26). Combined with the 2-px-thick outline
// drawn below, even small bubbles read clearly and big bubbles look properly
// substantial — matches the user's "крупнее (шире рисовка)" feedback.
static const float MIN_BASE_R = 6.0f;
static const float MAX_BASE_R = 26.0f;
static const float RATE       = 6.28f;             // 1 Hz oscillation
static const float PHASE_K    = 0.05f;             // per-bubble phase offset by x

// ---- State ----
struct ErBubble {
  int16_t x, y;
  float   baseR;     // 0 = empty slot
  int8_t  prevR;     // last drawn radius, 0 = nothing drawn
};

static ErBubble erBubbles[N_BUBBLES];
static float    er_t = 0.0f;

void initBubbles() {
  for (int i = 0; i < N_BUBBLES; i++) {
    erBubbles[i].x = 0;
    erBubbles[i].y = 0;
    erBubbles[i].baseR = 0.0f;
    erBubbles[i].prevR = 0;
  }
  er_t = 0.0f;
}

void stepBubbles() {
  // Advance time.
  er_t += SAVER_FRAME_MS / 1000.0f;
  if (er_t > 1000.0f) er_t = 0.0f;

  // Maybe spawn a new bubble at a random slot index — same trick as ER-301
  // (`int j = p * (N - 1)` so larger p picks higher slots; the same p also
  // controls the radius via p*p, so big bubbles are rarer).
  if (randFloat(0.0f, 1.0f) < SPAWN_PROB_PER_FRAME) {
    float p = randFloat(0.0f, 1.0f);
    int j = (int)(p * (N_BUBBLES - 1));
    ErBubble &b = erBubbles[j];

    // Clear any leftover outline (both rings — see eraseBubble below).
    if (b.prevR > 0) {
      tft.drawCircle(b.x, b.y, b.prevR, COLOR_BG);
      if (b.prevR > 2) tft.drawCircle(b.x, b.y, b.prevR - 1, COLOR_BG);
      b.prevR = 0;
    }
    b.x = (int16_t)(8 + (int)random(SCREEN_W - 16));
    b.y = (int16_t)(8 + (int)random(SCREEN_H - 16));
    b.baseR = p * p * (MAX_BASE_R - MIN_BASE_R) + MIN_BASE_R;
  }

  // Erase prev frame, redraw current radius for every active bubble.
  // Outline is drawn 2 pixels thick (radii r and r-1) when r is large enough
  // to make the second ring meaningful — r=1 would degenerate to a single
  // dot if we drew at r-1=0.
  for (int i = 0; i < N_BUBBLES; i++) {
    ErBubble &b = erBubbles[i];
    if (b.baseR <= 0.0f) continue;

    if (b.prevR > 0) {
      tft.drawCircle(b.x, b.y, b.prevR, COLOR_BG);
      if (b.prevR > 2) tft.drawCircle(b.x, b.y, b.prevR - 1, COLOR_BG);
      b.prevR = 0;
    }

    int r = (int)(b.baseR * sinf(b.x * PHASE_K + er_t * RATE));
    if (r > 0) {
      tft.drawCircle(b.x, b.y, r, COLOR_ACCENT);
      if (r > 2) tft.drawCircle(b.x, b.y, r - 1, COLOR_ACCENT);
      b.prevR = (int8_t)r;
    }
  }
}
