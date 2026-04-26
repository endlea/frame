// config.h — all tunable constants live here.
// Edit this file to change pins, screen size, colors, gamma, touch flags, layout.
#pragma once
#include <Arduino.h>

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

// =================== Theme (RGB565) ===================
#define COLOR_BG            0x0000
#define COLOR_FG            0xFFFF
#define COLOR_ACCENT        0x07FF
#define COLOR_BUTTON        0x2945
#define COLOR_BUTTON_TXT    0xFFFF
#define COLOR_DIVIDER       0x4208
#define COLOR_HEADER        0x10A2
#define COLOR_DIM           0x8410
#define COLOR_HIGHLIGHT_BG  0x10A2   // background of the row marked as "now playing"

// =================== Gamma + white balance ===================
// GAMMA: 1.0 = identity; >1.0 = darker, more depth; <1.0 = brighter.
// 1.0 = brightest/flat, 1.3 = bright with mild depth, 1.8 = soft with depth, 2.2 = sRGB-like.
static const float GAMMA  = 1.3f;
// White balance to compensate for the cool TFT backlight.
// B_GAIN < 1.0 -> suppress blue (warmer); R_GAIN > 1.0 -> boost red.
static const float R_GAIN = 1.05f;
static const float G_GAIN = 1.00f;
static const float B_GAIN = 0.85f;

// =================== Touch coordinate mapping ===================
// Defaults match Adafruit 2090 + ILI9341 setRotation(0) (portrait, USB at bottom).
// Flip these if your physical mounting differs.
static const bool TOUCH_FLIP_X  = true;
static const bool TOUCH_FLIP_Y  = true;
static const bool TOUCH_SWAP_XY = false;

// =================== Layout ===================
static const int HEADER_H = 50;
static const int BACK_BUTTON_W = 50;
static const int LIST_ITEM_H = 50;
static const int OPTIONS_LIST_TOP = 90;            // first row in the Options screen

// =================== Menu animation ===================
static const uint32_t SAVER_FRAME_MS = 33;         // ~30 fps cap for the animated background

// =================== Touch tuning ===================
static const int   TAP_DRAG_THRESHOLD_PX = 12;     // movement above this counts as drag, not tap
static const uint32_t TAP_MAX_DURATION_MS = 800;   // long press above this is no longer a tap