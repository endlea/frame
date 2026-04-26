// config.h — all tunable constants live here.
// Edit this file to change pins, screen size, gamma, touch flags, layout.
//
// Theming note: the COLOR_* macros used to live here. They moved to theme.h
// so they can switch at runtime (Options → UI COLOR). config.h pulls theme.h
// in so every file that includes config.h still sees COLOR_BG etc.
#pragma once
#include <Arduino.h>
#include "theme.h"
#include "screensaver.h"

// =================== Display pins ===================
#define TFT_CS  10
#define TFT_DC  9
#define TFT_RST 8

// =================== Display ===================
static const uint32_t SPI_CLOCK = 48000000;
static const int SCREEN_W = 240;
static const int SCREEN_H = 320;
static const uint8_t ROTATION = 0;          // 0 = portrait, 1 = landscape

// =================== SD ===================
#define GIF_DIR "/gifs"
static const int MAX_FILES = 128;

// =================== Gamma + white balance ===================
// GAMMA: 1.0 = identity; >1.0 = darker, more depth; <1.0 = brighter.
static const float GAMMA  = 1.3f;
// White balance to compensate for the cool TFT backlight.
static const float R_GAIN = 1.05f;
static const float G_GAIN = 1.00f;
static const float B_GAIN = 0.85f;

// =================== Touch coordinate mapping ===================
static const bool TOUCH_FLIP_X  = true;
static const bool TOUCH_FLIP_Y  = true;
static const bool TOUCH_SWAP_XY = false;

// =================== Layout ===================
static const int HEADER_H = 50;
static const int BACK_BUTTON_W = 64;
static const int LIST_ITEM_H = 50;                 // GIF browser row height

// Main menu: thin title strip ("Frame" + status line) instead of the old
// 100-px colored band. The screensaver gets the rest of the screen.
static const int TITLE_BAND_H = 50;

// Options screen — two sections (SCREENSAVER, UI COLOR), now WITH vertical
// scrolling so rows can be comfortably tappable instead of cramming
// everything into one screen. The constants below are CONTENT-relative
// (y=0 means the top of the scrollable area, just below the header).
//
// Item height bumped 24 → 44 so a finger has a much bigger target. Section
// labels also got a bit more breathing room (18 → 28).
//
// Layout in content-coords:
//   [0 .. OPTIONS_SAVER_TOP)       SCREENSAVER label band
//   [OPTIONS_SAVER_TOP .. OPTIONS_SAVER_BOTTOM)
//                                  6 saver rows
//   [OPTIONS_SAVER_BOTTOM .. OPTIONS_COLOR_TOP)
//                                  UI COLOR label band
//   [OPTIONS_COLOR_TOP .. OPTIONS_COLOR_BOTTOM)
//                                  3 ui-colour rows
//   OPTIONS_CONTENT_H              total scrollable height
//
static const int OPTIONS_LABEL_H       = 28;
static const int OPTIONS_ITEM_H        = 44;

// Dynamic layout — no hardcoded counts
static const int OPTIONS_SAVER_TOP     = OPTIONS_LABEL_H;
static const int OPTIONS_SAVER_BOTTOM  = OPTIONS_SAVER_TOP + SAVER_COUNT * OPTIONS_ITEM_H;

static const int OPTIONS_COLOR_TOP     = OPTIONS_SAVER_BOTTOM + OPTIONS_LABEL_H;
static const int OPTIONS_COLOR_BOTTOM  = OPTIONS_COLOR_TOP + UI_COLOR_COUNT * OPTIONS_ITEM_H;

static const int OPTIONS_CONTENT_H     = OPTIONS_COLOR_BOTTOM;

// =================== Menu animation ===================
// Cap at ~25ms (≈40fps target). Real cap is the ILI9341 DMA push (~25ms for
// 240*320 RGB565 at 48MHz SPI), so anything below this just spins the loop.
static const uint32_t SAVER_FRAME_MS = 25;

// =================== Idle behaviour ===================
// After this many ms with no touch in the main menu, the chrome fades away
// and only the screensaver remains. The saver itself is NOT reset on the
// idle/wake transition — its particle state continues without interruption
// so it reads as "the chrome melted away," not "the screensaver restarted."
// Any tap wakes the menu up and is consumed (doesn't activate a button).
static const uint32_t IDLE_TIMEOUT_MS = 30000;     // 30s

// =================== Touch tuning ===================
static const int   TAP_DRAG_THRESHOLD_PX = 18;     // movement above this counts as drag, not tap
static const uint32_t TAP_MAX_DURATION_MS = 800;   // long press above this is no longer a tap

// =================== GIF speed / BPM screen ===================
static const float GIF_SPEED_MIN = 0.50f;
static const float GIF_SPEED_MAX = 2.00f;

static const int SPEED_SLIDER_X = 24;
static const int SPEED_SLIDER_Y = 238;
static const int SPEED_SLIDER_W = 192;
static const int SPEED_SLIDER_H = 34;
