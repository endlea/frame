// theme.h — runtime UI palette. Three themes: WHITE / YELLOW / LEGACY.
//
// All COLOR_* macros throughout the project go through themeColor(), so
// switching `currentUiColor` reskins the entire UI on the next redraw.
// The palette tables themselves live in theme.cpp.
//
// Why macros instead of plain function calls everywhere: existing code (and
// every screensaver) already uses bare names like COLOR_BG, COLOR_ACCENT.
// Keeping the macro spelling means we don't have to rewrite every call site
// to gain runtime theming.
#pragma once
#include <Arduino.h>

enum UiColor : uint8_t {
  UI_COLOR_WHITE,    // black/white minimal — Norns / Teletype vibe
  UI_COLOR_YELLOW,   // amber over black — Monome OLED look
  UI_COLOR_LEGACY,   // the original cyan-heavy palette
  UI_COLOR_COUNT
};

extern UiColor currentUiColor;
const char *uiColorName(UiColor c);

// Slot indices into the palette table (one row per UiColor).
enum {
  TC_BG = 0,
  TC_FG,
  TC_ACCENT,
  TC_BUTTON,
  TC_BUTTON_TXT,
  TC_DIVIDER,
  TC_HEADER,
  TC_DIM,
  TC_HIGHLIGHT_BG,
  TC_COUNT
};

uint16_t themeColor(int kind);

// Re-spelled COLOR_* macros so existing call sites keep working.
#define COLOR_BG            themeColor(TC_BG)
#define COLOR_FG            themeColor(TC_FG)
#define COLOR_ACCENT        themeColor(TC_ACCENT)
#define COLOR_BUTTON        themeColor(TC_BUTTON)
#define COLOR_BUTTON_TXT    themeColor(TC_BUTTON_TXT)
#define COLOR_DIVIDER       themeColor(TC_DIVIDER)
#define COLOR_HEADER        themeColor(TC_HEADER)
#define COLOR_DIM           themeColor(TC_DIM)
#define COLOR_HIGHLIGHT_BG  themeColor(TC_HIGHLIGHT_BG)
