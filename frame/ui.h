// ui.h — buttons, screens, and basic drawing primitives.
// All visuals (menu, browser, overlay) live here. Edit this file to redesign the UI.
#pragma once
#include <Arduino.h>
#include "config.h"

struct Button {
  int16_t x, y, w, h;
  const char *label;
};

// Button instances — tweak position/size/label here for layout changes.
extern const Button BTN_SELECT;
extern const Button BTN_OPTIONS;
extern const Button BTN_OVL_CONT;
extern const Button BTN_OVL_PICK;
extern const Button BTN_OVL_MENU;

// ---- Hit-testing ----
bool inButton(const Button &b, int16_t x, int16_t y);

// ---- Drawing primitives ----
void drawCenteredText(const char *s, int16_t cx, int16_t cy, uint8_t size, uint16_t color);
void drawButton(const Button &b, uint16_t bg, uint16_t fg);

// ---- Full-screen views ----
// Draws the static parts of the main menu (title strip, status, buttons) on
// top of the live screensaver framebuffer. Does NOT call fillScreen or
// updateScreen — caller is responsible. Call once on enterMenu() and again
// on every animation frame so the chrome always sits on top of the saver.
void drawMenuChrome();

// Repaints just the regions drawMenuChrome() covers back to COLOR_BG, leaving
// the rest of the framebuffer (the live screensaver) untouched. Used by the
// active→idle transition so the chrome "melts away" without resetting the
// saver's particle state.
void clearMenuChrome();

void drawBrowser();
void drawOverlay();
void drawOptions();

// Halve every channel of every pixel in the framebuffer (RGB565 fast pass).
void dimFrameBuffer();
