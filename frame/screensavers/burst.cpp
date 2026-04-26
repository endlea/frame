// burst.cpp — pixelated exploding stars. Custom pattern, no ER-301 reference.
//
// Multiple "bursts" run in parallel, each cycling through three phases:
//
//   GROW   (~10 frames)  a small "+" star grows at a random position
//   FLASH  (~3 frames)   one big bright ring drawn around the star, then erased
//   BURST  (~50 frames)  16 radial particles fly out, decelerate, fade, die
//   DEAD                 wait for the cooldown, respawn at a new spot
//
// Visual style is intentionally chunky — every particle is a 2x2 block instead
// of a single pixel — so it reads as "8-bit" / arcade fireworks rather than a
// smooth modern animation. Colours are picked per burst from a bright palette
// so two simultaneous explosions usually have different hues.

#include "../config.h"
#include "internal.h"
#include "helpers.h"
#include <ILI9341_t3n.h>
#include <math.h>

extern ILI9341_t3n tft;

// ---- Tunables ----
// Density bumps for a busier sky: more concurrent bursts, more particles per
// burst, and a shorter cooldown between spawns. The hot loop is still N²-free
// so this stays under per-frame budget on Teensy 4.1.
static const int   N_BURSTS         = 6;
static const int   PARTS_PER_BURST  = 22;
static const int   GROW_FRAMES      = 10;       // ~250ms at 40fps
static const int   FLASH_FRAMES     = 3;        // brief bright ring
static const int   MAX_BURST_AGE    = 60;       // hard timeout safety net
static const int   SPAWN_COOLDOWN   = 4;        // frames between spawns (was 8)

enum Phase : uint8_t { PH_DEAD, PH_GROW, PH_FLASH, PH_BURST };

struct Particle {
  float    x, y;
  float    vx, vy;
  int16_t  prevX, prevY;
  uint8_t  life;          // 0 = dead
};

struct Burst {
  Phase    phase;
  uint8_t  age;           // frames in current phase
  int16_t  cx, cy;
  uint8_t  prevStarSize;  // last-drawn "+" half-arm length, 0 = nothing
  uint8_t  prevFlashR;    // last-drawn flash ring radius
  Particle parts[PARTS_PER_BURST];
};

static Burst   bursts[N_BURSTS];
static uint8_t cooldown = 0;

// Per-burst colour was a 7-entry rainbow palette. With the WHITE/YELLOW/LEGACY
// theme system everything else now leans monochrome, so bursts pick up the
// active COLOR_ACCENT every frame instead. One side benefit: we no longer have
// to remember which colour a particular burst spawned with — its hue follows
// the theme even mid-flight.

// ---- Drawing helpers ----
// 3x3 chunky pixel (was 2x2) — bigger blocks match the "крупнее (шире
// рисовка)" direction taken across the rest of the UI. Plus-star and particle
// spacing below still use *2 increments, so adjacent blocks overlap by 1 px,
// producing solid arms instead of gappy ones.
static inline void pixelBlock(int16_t x, int16_t y, uint16_t color) {
  if (x < 0 || y < 0 || x >= SCREEN_W - 2 || y >= SCREEN_H - 2) return;
  tft.fillRect(x, y, 3, 3, color);
}

// "+" plus four diagonal accents, half-arm length = size.
static void drawPlusStar(int16_t cx, int16_t cy, uint8_t size, uint16_t color) {
  for (int i = -(int)size; i <= (int)size; i++) {
    pixelBlock(cx + i * 2, cy, color);
    pixelBlock(cx, cy + i * 2, color);
  }
  if (size >= 2) {
    int d = (int)size - 1;
    pixelBlock(cx + d * 2, cy + d * 2, color);
    pixelBlock(cx - d * 2, cy + d * 2, color);
    pixelBlock(cx + d * 2, cy - d * 2, color);
    pixelBlock(cx - d * 2, cy - d * 2, color);
  }
}

// ---- Lifecycle ----
static void spawnParticles(Burst &b) {
  for (int i = 0; i < PARTS_PER_BURST; i++) {
    Particle &p = b.parts[i];
    // Even-spaced base angle plus jitter so particles don't form a rigid wheel.
    float angle = (2.0f * 3.14159f * i) / PARTS_PER_BURST + randFloat(-0.18f, 0.18f);
    float speed = randFloat(2.2f, 3.6f);
    p.x = b.cx;
    p.y = b.cy;
    p.vx = cosf(angle) * speed;
    p.vy = sinf(angle) * speed;
    p.prevX = b.cx;
    p.prevY = b.cy;
    p.life = (uint8_t)randInt(22, 40);
  }
}

static void respawnBurst(Burst &b) {
  b.phase = PH_GROW;
  b.age = 0;
  b.cx = (int16_t)randInt(20, SCREEN_W - 20);
  b.cy = (int16_t)randInt(20, SCREEN_H - 20);
  b.prevStarSize = 0;
  b.prevFlashR = 0;
  for (int i = 0; i < PARTS_PER_BURST; i++) b.parts[i].life = 0;
}

void initBurst() {
  for (int i = 0; i < N_BURSTS; i++) {
    bursts[i].phase = PH_DEAD;
    bursts[i].age = 0;
    bursts[i].prevStarSize = 0;
    bursts[i].prevFlashR = 0;
    for (int j = 0; j < PARTS_PER_BURST; j++) bursts[i].parts[j].life = 0;
  }
  cooldown = 0;
}

void stepBurst() {
  if (cooldown > 0) cooldown--;

  for (int b = 0; b < N_BURSTS; b++) {
    Burst &br = bursts[b];

    // -------- DEAD: maybe respawn ----------
    if (br.phase == PH_DEAD) {
      if (cooldown == 0) {
        respawnBurst(br);
        cooldown = SPAWN_COOLDOWN;
      }
      continue;
    }

    // -------- GROW: "+" star grows from 1 → 4 ----------
    if (br.phase == PH_GROW) {
      if (br.prevStarSize > 0) drawPlusStar(br.cx, br.cy, br.prevStarSize, COLOR_BG);
      br.age++;
      uint8_t size = (uint8_t)(br.age / 2);
      if (size > 4) size = 4;
      drawPlusStar(br.cx, br.cy, size, COLOR_ACCENT);
      br.prevStarSize = size;

      if (br.age >= GROW_FRAMES) {
        // Erase the star — the flash ring will replace it for a few frames.
        drawPlusStar(br.cx, br.cy, br.prevStarSize, COLOR_BG);
        br.prevStarSize = 0;
        br.phase = PH_FLASH;
        br.age = 0;
      }
      continue;
    }

    // -------- FLASH: short bright ring before the burst ----------
    if (br.phase == PH_FLASH) {
      if (br.prevFlashR > 0) tft.drawCircle(br.cx, br.cy, br.prevFlashR, COLOR_BG);
      br.age++;
      uint8_t flashR = (uint8_t)(4 + br.age * 3);
      tft.drawCircle(br.cx, br.cy, flashR, COLOR_ACCENT);
      br.prevFlashR = flashR;

      if (br.age >= FLASH_FRAMES) {
        tft.drawCircle(br.cx, br.cy, br.prevFlashR, COLOR_BG);
        br.prevFlashR = 0;
        spawnParticles(br);
        br.phase = PH_BURST;
        br.age = 0;
      }
      continue;
    }

    // -------- BURST: animate radial particles ----------
    bool anyAlive = false;
    for (int i = 0; i < PARTS_PER_BURST; i++) {
      Particle &p = br.parts[i];

      // Erase prev block (no-op if life was already 0).
      pixelBlock(p.prevX, p.prevY, COLOR_BG);
      if (p.life == 0) continue;

      // Advance with mild drag + tiny gravity so particles arc instead of
      // flying out in perfect straight lines.
      p.x += p.vx;
      p.y += p.vy;
      p.vx *= 0.97f;
      p.vy *= 0.97f;
      p.vy += 0.04f;
      p.life--;

      if (p.x < 0 || p.x >= SCREEN_W - 1 ||
          p.y < 0 || p.y >= SCREEN_H - 1) {
        p.life = 0;
        continue;
      }

      // Fade colour by halving channels in the last few frames of life.
      uint16_t color = COLOR_ACCENT;
      if (p.life < 8) color = (uint16_t)((color >> 1) & 0x7BEF);

      int16_t nx = (int16_t)p.x;
      int16_t ny = (int16_t)p.y;
      pixelBlock(nx, ny, color);
      p.prevX = nx;
      p.prevY = ny;
      anyAlive = true;
    }

    br.age++;
    if (!anyAlive || br.age > MAX_BURST_AGE) br.phase = PH_DEAD;
  }
}
