// fractals.cpp — stable themed Mandelbrot breathing zoom.
// Full-frame render every step: no stripes, no partial redraw glitches.

#include "../config.h"
#include "internal.h"
#include "helpers.h"
#include <ILI9341_t3n.h>
#include <math.h>

extern ILI9341_t3n tft;

// ---- Tunables ----
static const int FR_SCALE    = 2;
static const int FR_MAX_ITER = 76;

static const int FR_DIVE_FRAMES = 420;
static const int FR_PULL_FRAMES = 170;

static const double FR_MIN_ZOOM = 1.25;
static const double FR_MAX_ZOOM = 2600.0;

// Seahorse Valley.
static const double FR_BASE_X = -0.743643887037151;
static const double FR_BASE_Y =  0.131825904205330;

static int fr_frame = 0;
static bool fr_pulling = false;

static double fr_t = 0.0;
static double fr_angle = 0.0;

static double fr_centerX = FR_BASE_X;
static double fr_centerY = FR_BASE_Y;

static inline double frEase(double t) {
  if (t < 0.0) t = 0.0;
  if (t > 1.0) t = 1.0;

  // smootherstep
  return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

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

  if (v < 120) {
    return frBlend565(COLOR_BG, COLOR_ACCENT, (uint8_t)(v * 2));
  }

  return frBlend565(COLOR_ACCENT, COLOR_FG, (uint8_t)((v - 120) * 2));
}

static double frCurrentZoom() {
  double p;

  if (!fr_pulling) {
    p = frEase((double)fr_frame / (double)FR_DIVE_FRAMES);
  } else {
    p = 1.0 - frEase((double)fr_frame / (double)FR_PULL_FRAMES);
  }

  return FR_MIN_ZOOM * pow(FR_MAX_ZOOM / FR_MIN_ZOOM, p);
}

static uint16_t frMandelPixel(int px, int py) {
  double zoom = frCurrentZoom();
  double aspect = (double)SCREEN_W / (double)SCREEN_H;
  double view = 3.0 / zoom;

  double cx = fr_centerX +
              (((double)px / (double)SCREEN_W) - 0.5) * view * aspect;

  double cy = fr_centerY +
              (((double)py / (double)SCREEN_H) - 0.5) * view;

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

static void frNextNearbyPoint() {
  // Small movement around Seahorse Valley. Too large = black interior/glitch.
  fr_angle += 0.61;

  double r = 0.0018 + 0.0011 * sin(fr_angle * 0.9);

  fr_centerX = FR_BASE_X + r * cos(fr_angle);
  fr_centerY = FR_BASE_Y + r * sin(fr_angle * 1.2);
}

void initFractals() {
  fr_frame = 0;
  fr_pulling = false;

  fr_t = 0.0;
  fr_angle = 0.0;

  fr_centerX = FR_BASE_X;
  fr_centerY = FR_BASE_Y;

  tft.fillScreen(COLOR_BG);
}

void stepFractals() {
  fr_t += 1.0;
  fr_frame++;

  if (!fr_pulling && fr_frame >= FR_DIVE_FRAMES) {
    fr_frame = 0;
    fr_pulling = true;
  } else if (fr_pulling && fr_frame >= FR_PULL_FRAMES) {
    fr_frame = 0;
    fr_pulling = false;
    frNextNearbyPoint();
  }

  for (int y = 0; y < SCREEN_H; y += FR_SCALE) {
    for (int x = 0; x < SCREEN_W; x += FR_SCALE) {
      tft.fillRect(x, y, FR_SCALE, FR_SCALE, frMandelPixel(x, y));
    }
  }
}