#include "screensaver.h"
#include "config.h"
#include <ILI9341_t3n.h>
#include <math.h>

// `tft` is defined in gif_player_ui.ino.
extern ILI9341_t3n tft;

// Persists across enterMenu() calls — the user's choice from the Options screen.
SaverPattern currentSaver = SAVER_BUBBLES;

// =================== Helpers ===================
static int randInt(int lo, int hi) {
  return lo + (int)random(hi - lo);
}

static float randFloat(float lo, float hi) {
  return lo + (hi - lo) * (float)random(10000) / 10000.0f;
}

// =================== BUBBLES ===================
// Soft balls bouncing off walls and off each other (elastic, equal mass).
struct Bubble {
  int16_t prevX, prevY;
  float x, y;
  float vx, vy;
  uint8_t r;
  uint16_t color;
};

static const int N_BUBBLES = 12;
static Bubble bubbles[N_BUBBLES];

static const uint16_t BUBBLE_COLORS[] = {
  0x07FF,   // cyan
  0x6E1F,   // sky blue
  0x4FFF,   // pale cyan
  0x861F,   // periwinkle
  0xAF9F,   // pale lavender
  0x4E1F,   // medium blue
};
static const int N_BUBBLE_COLORS =
  sizeof(BUBBLE_COLORS) / sizeof(BUBBLE_COLORS[0]);

static void initBubble(Bubble &b) {
  b.r = (uint8_t)randInt(4, 9);
  b.x = randFloat(b.r + 2, SCREEN_W - b.r - 2);
  b.y = randFloat(b.r + 2, SCREEN_H - b.r - 2);
  b.vx = randFloat(-1.0f, 1.0f);
  b.vy = randFloat(-1.0f, 1.0f);
  // avoid duds that barely move
  if (fabsf(b.vx) < 0.25f) b.vx = (b.vx < 0 ? -0.5f : 0.5f);
  if (fabsf(b.vy) < 0.25f) b.vy = (b.vy < 0 ? -0.5f : 0.5f);
  b.color = BUBBLE_COLORS[random(N_BUBBLE_COLORS)];
  b.prevX = (int16_t)b.x;
  b.prevY = (int16_t)b.y;
}

static void initBubbles() {
  for (int i = 0; i < N_BUBBLES; i++) initBubble(bubbles[i]);
}

static void stepBubbles() {
  // 1. erase old positions
  for (int i = 0; i < N_BUBBLES; i++) {
    Bubble &b = bubbles[i];
    tft.fillCircle(b.prevX, b.prevY, b.r, COLOR_BG);
  }

  // 2. move + bounce off walls
  for (int i = 0; i < N_BUBBLES; i++) {
    Bubble &b = bubbles[i];
    b.x += b.vx;
    b.y += b.vy;
    if (b.x < b.r)               { b.x = b.r;               b.vx = -b.vx; }
    if (b.x > SCREEN_W - b.r)    { b.x = SCREEN_W - b.r;    b.vx = -b.vx; }
    if (b.y < b.r)               { b.y = b.r;               b.vy = -b.vy; }
    if (b.y > SCREEN_H - b.r)    { b.y = SCREEN_H - b.r;    b.vy = -b.vy; }
  }

  // 3. ball-ball collisions (elastic, equal mass) — N^2 but N is small
  for (int i = 0; i < N_BUBBLES; i++) {
    for (int j = i + 1; j < N_BUBBLES; j++) {
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
        // push apart so they don't stick
        float overlap = minDist - dist;
        a.x -= nx * overlap * 0.5f;
        a.y -= ny * overlap * 0.5f;
        c.x += nx * overlap * 0.5f;
        c.y += ny * overlap * 0.5f;
        // exchange velocity component along the contact normal
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

  // 4. draw new positions (with a tiny shine pixel)
  for (int i = 0; i < N_BUBBLES; i++) {
    Bubble &b = bubbles[i];
    int16_t nx = (int16_t)b.x;
    int16_t ny = (int16_t)b.y;
    tft.fillCircle(nx, ny, b.r, b.color);
    if (b.r >= 4) {
      tft.fillCircle(nx - b.r / 3, ny - b.r / 3, 1, COLOR_FG);
    }
    b.prevX = nx;
    b.prevY = ny;
  }
}

// =================== STARS ===================
// Stationary pixels that twinkle in place. Half cool-tinted, half warm-tinted,
// rest pure white. No erase needed — each star simply redraws every frame and
// the chrome covers the ones underneath it.
struct Star {
  int16_t x, y;
  uint8_t base;     // peak brightness (8..31)
  uint8_t phase;    // current brightness 0..base
  int8_t  step;     // +1 or -1
  uint8_t kind;     // 0 = white, 1 = warm, 2 = cool
};

static const int N_STARS = 70;
static Star stars[N_STARS];

static void initStars() {
  for (int i = 0; i < N_STARS; i++) {
    stars[i].x = (int16_t)randInt(0, SCREEN_W);
    stars[i].y = (int16_t)randInt(0, SCREEN_H);
    stars[i].base = (uint8_t)randInt(10, 32);
    stars[i].phase = (uint8_t)randInt(0, stars[i].base);
    stars[i].step = (random(2) == 0) ? -1 : 1;
    stars[i].kind = (uint8_t)random(3);
  }
}

static void stepStars() {
  for (int i = 0; i < N_STARS; i++) {
    Star &s = stars[i];
    int p = (int)s.phase + s.step;
    if (p < 0)         { p = 0;          s.step =  1; }
    if (p > s.base)    { p = s.base;     s.step = -1; }
    s.phase = (uint8_t)p;

    uint8_t r5 = s.phase;
    uint8_t g6 = s.phase * 2;
    uint8_t b5 = s.phase;
    if (s.kind == 1)      b5 = s.phase >> 1;   // warm: less blue
    else if (s.kind == 2) r5 = s.phase >> 1;   // cool: less red
    if (g6 > 63) g6 = 63;

    uint16_t color = (uint16_t(r5) << 11) | (uint16_t(g6) << 5) | uint16_t(b5);
    tft.drawPixel(s.x, s.y, color);
  }
}

// =================== SNOW ===================
// Flakes drifting downwards with a sine-wave horizontal sway.
struct Flake {
  int16_t prevX, prevY;
  float x, y;
  float vy;
  float swayAmp, swayPhase;
  uint8_t r;
  uint16_t color;
};

static const int N_FLAKES = 22;
static Flake flakes[N_FLAKES];

static void initFlake(Flake &f, bool atTop) {
  f.x = randFloat(0, SCREEN_W);
  f.y = atTop ? randFloat(-30, 0) : randFloat(0, SCREEN_H);
  f.vy = randFloat(0.4f, 1.2f);
  f.swayAmp = randFloat(0.2f, 0.7f);
  f.swayPhase = randFloat(0.0f, 6.28f);
  f.r = (uint8_t)randInt(1, 3);
  uint8_t shade = (uint8_t)randInt(22, 32);
  uint8_t g6 = shade * 2;
  if (g6 > 63) g6 = 63;
  f.color = (uint16_t(shade) << 11) | (uint16_t(g6) << 5) | uint16_t(shade);
  f.prevX = (int16_t)f.x;
  f.prevY = (int16_t)f.y;
}

static void initSnow() {
  for (int i = 0; i < N_FLAKES; i++) initFlake(flakes[i], false);
}

static void stepSnow() {
  for (int i = 0; i < N_FLAKES; i++) {
    Flake &f = flakes[i];

    tft.fillCircle(f.prevX, f.prevY, f.r, COLOR_BG);

    f.swayPhase += 0.06f;
    f.y += f.vy;
    f.x += sinf(f.swayPhase) * f.swayAmp;

    if (f.y > SCREEN_H + 4) initFlake(f, true);
    if (f.x < 0)            f.x = SCREEN_W;
    if (f.x > SCREEN_W)     f.x = 0;

    int16_t nx = (int16_t)f.x;
    int16_t ny = (int16_t)f.y;
    tft.fillCircle(nx, ny, f.r, f.color);
    f.prevX = nx;
    f.prevY = ny;
  }
}

// =================== Dispatcher ===================
const char *saverName(SaverPattern p) {
  switch (p) {
    case SAVER_NONE:    return "OFF";
    case SAVER_BUBBLES: return "BUBBLES";
    case SAVER_STARS:   return "STARS";
    case SAVER_SNOW:    return "SNOW";
    default:            return "?";
  }
}

void saverInit(SaverPattern p) {
  currentSaver = p;
  switch (p) {
    case SAVER_BUBBLES: initBubbles(); break;
    case SAVER_STARS:   initStars();   break;
    case SAVER_SNOW:    initSnow();    break;
    default: break;
  }
}

void saverStep() {
  switch (currentSaver) {
    case SAVER_BUBBLES: stepBubbles(); break;
    case SAVER_STARS:   stepStars();   break;
    case SAVER_SNOW:    stepSnow();    break;
    default: break;
  }
}