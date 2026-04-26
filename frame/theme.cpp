// theme.cpp — palette tables and the themeColor() lookup.
//
// PALETTE[theme][slot] -> RGB565. To add a new theme: add an enum value to
// theme.h's UiColor, append a row here, extend uiColorName(), and bump the
// scrolling list math in ui.cpp if needed.
#include "theme.h"

// Boot default — White is the "minimal" look the user asked for.
UiColor currentUiColor = UI_COLOR_WHITE;

static const uint16_t PALETTE[UI_COLOR_COUNT][TC_COUNT] = {
  // -------- UI_COLOR_WHITE (monochrome black/white) --------
  {
    /* TC_BG */            0x0000,
    /* TC_FG */            0xFFFF,
    /* TC_ACCENT */        0xFFFF,   // same as FG — accents are pure white
    /* TC_BUTTON */        0x0000,   // outlined buttons (BG fill, FG border)
    /* TC_BUTTON_TXT */    0xFFFF,
    /* TC_DIVIDER */       0x4208,   // medium gray
    /* TC_HEADER */        0x0000,   // no colored band — just BG behind title
    /* TC_DIM */           0x8410,   // ~50% gray
    /* TC_HIGHLIGHT_BG */  0x2104,   // very dark gray for the active row
  },
  // -------- UI_COLOR_YELLOW (Monome amber) --------
  {
    /* TC_BG */            0x0000,
    /* TC_FG */            0xFEC0,   // soft amber
    /* TC_ACCENT */        0xFFE0,   // bright yellow for highlights
    /* TC_BUTTON */        0x0000,
    /* TC_BUTTON_TXT */    0xFEC0,
    /* TC_DIVIDER */       0x4200,   // dark amber
    /* TC_HEADER */        0x0000,
    /* TC_DIM */           0x8420,   // muted amber
    /* TC_HIGHLIGHT_BG */  0x2100,   // very dark amber
  },
  // -------- UI_COLOR_LEGACY (the original cyan palette) --------
  {
    /* TC_BG */            0x0000,
    /* TC_FG */            0xFFFF,
    /* TC_ACCENT */        0x07FF,   // cyan
    /* TC_BUTTON */        0x2945,   // dark blue-gray fill
    /* TC_BUTTON_TXT */    0xFFFF,
    /* TC_DIVIDER */       0x4208,
    /* TC_HEADER */        0x10A2,   // dark blue band
    /* TC_DIM */           0x8410,
    /* TC_HIGHLIGHT_BG */  0x10A2,
  },
};

uint16_t themeColor(int kind) {
  return PALETTE[currentUiColor][kind];
}

const char *uiColorName(UiColor c) {
  switch (c) {
    case UI_COLOR_WHITE:  return "WHITE";
    case UI_COLOR_YELLOW: return "YELLOW";
    case UI_COLOR_LEGACY: return "LEGACY";
    default:              return "?";
  }
}
