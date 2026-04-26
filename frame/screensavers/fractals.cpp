// fractals.cpp — safe themed Mandelbrot deep zoom.
// Always zooms inward. If the view becomes almost black,
// it silently jumps to another target from darkness.

#include "../config.h"
#include "internal.h"
#include "helpers.h"
#include <ILI9341_t3n.h>
#include <math.h>

extern ILI9341_t3n tft;

// ---- Tunables ----
static const int    FR_SCALE      = 2;
static const int    FR_MAX_ITER   = 72;
static const double FR_ZOOM_STEP  = 1.012;

// Mandelbrot zoom targets.
static const double FR_TARGET_XS[] = {
  -0.743643887037151,
  -0.761574,
  -0.101096,
  -1.25066,
  -0.15652
};

static const double FR_TARGET_YS[] = {
   0.131825904205330,
  -0.0847596,
   0.956286,
   0.02012,
   1.03225
};

static const int FR_N_TARGETS = sizeof(FR_TARGET_XS) / sizeof(FR_TARGET_XS[0]);

static int    fr_target = 0;
static int    fr_blackFrames = 0;
static double fr_zoom = 1.0;
static double fr_t = 0.0;

static inline uint16_t frBlend565(uint16_t a, uint16_t b, uint8_t amount) {
  uint8_t ar = (a >> 11) & 0x1F;
  uint8_t ag = (a >> 5)  & 0x3F;
  uint8_t ab = a & 0x1F;

  uint8_t br = (b >> 11) & 0x1F;
  uint8_t bg = (b >> 5)  & 0x3F;
  uint8_t bb = b & 0x1F;

  uint8_t r  = ar + (((int)br - ar) * amount >> 8);
  uint8_t g  = ag + (((int)bg - ag) * amount >> 8);
  uint8_t bl = ab + (((int)bb - ab) * amount >> 8);

  return (r << 11) | (g << 5) | bl;
}

static uint16_t frColor(int iter) {
  if (iter >= FR_MAX_ITER) return COLOR_BG;

  uint8_t v = (uint8_t)((iter * 255) / FR_MAX_ITER);

  // Theme-safe palette only:
  // BG -> ACCENT -> FG. No raw white/blue constants.
  if (v < 120) {
    return frBlend565(COLOR_BG, COLOR_ACCENT, (uint8_t)(v * 2));
  }

  return frBlend565(COLOR_ACCENT, COLOR_FG, (uint8_t)((v - 120) * 2));
}

static uint16_t frMandelPixel(int px, int py) {
  double aspect = (double)SCREEN_W / (double)SCREEN_H;
  double view = 3.0 / fr_zoom;

  double drift = view * 0.018;

  double centerX = FR_TARGET_XS[fr_target] + drift * sin(fr_t * 0.011);
  double centerY = FR_TARGET_YS[fr_target] + drift * cos(fr_t * 0.013);

  double cx = centerX + (((double)px / SCREEN_W) - 0.5) * view * aspect;
  double cy = centerY + (((double)py / SCREEN_H) - 0.5) * view;

  double x = 0.0;
  double y = 0.0;

  int iter = 0;

  while (x * x + y * y <= 4.0 && iter < FR_MAX_ITER) {
    double xx = x * x - y * y + cx;
    y = 2.0 * x * y + cy;
    x = xx;
    iter++;
  }

  return frColor(iter);
}

static void frNextTarget() {
  fr_target = (fr_target + 1) % FR_N_TARGETS;
  fr_zoom = 1.0;
  fr_blackFrames = 0;
  fr_t += 37.0;

  tft.fillScreen(COLOR_BG);
}

void initFractals() {
  fr_target = 0;
  fr_blackFrames = 0;
  fr_zoom = 1.0;
  fr_t = 0.0;

  tft.fillScreen(COLOR_BG);
}

void stepFractals() {
  fr_t += 1.0;
  fr_zoom *= FR_ZOOM_STEP;

  int blackCount = 0;
  int totalCount = 0;

  for (int y = 0; y < SCREEN_H; y += FR_SCALE) {
    for (int x = 0; x < SCREEN_W; x += FR_SCALE) {
      uint16_t c = frMandelPixel(x, y);

      if (c == COLOR_BG) blackCount++;
      totalCount++;

      tft.fillRect(x, y, FR_SCALE, FR_SCALE, c);
    }
  }

  // If the image has collapsed into mostly interior black,
  // wait a few frames, then start another inward dive.
  if (blackCount > totalCount * 85 / 100) {
    fr_blackFrames++;
  } else {
    fr_blackFrames = 0;
  }

  if (fr_blackFrames > 8) {
    frNextTarget();
  }
}