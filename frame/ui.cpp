#include "ui.h"
#include "gif_engine.h"     // fileNames, fileCount
#include "screensaver.h"    // currentSaver, saverName, SAVER_COUNT
#include "theme.h"          // currentUiColor, uiColorName, UI_COLOR_COUNT
#include "clock_input.h"    // clockActive, clockBpm, clockPulseBrightness
#include <ILI9341_t3n.h>

// `tft` is defined in gif_player_ui.ino.
extern ILI9341_t3n tft;

// `fbBackup`, `browserScrollY`, `optionsScrollY` and `playingName` are owned
// by gif_player_ui.ino. drawOverlay() copies the live frame into fbBackup so
// "Continue" can restore it. drawBrowser/drawOptions read their scroll vars
// for list position; playingName tells drawBrowser which row to highlight.
extern uint16_t fbBackup[];
extern int browserScrollY;
extern int optionsScrollY;
extern String playingName;

// =================== Button instances ===================
// Layout note: with the redesign, the title is now a thin TITLE_BAND_H strip
// at the top — the rest of the screen is mostly screensaver. Buttons stay
// near the bottom (175 / 250) so the saver has the largest possible area
// between the title and the buttons.
const Button BTN_SELECT    = { 20, 175, 200, 60, "SELECT GIF" };
const Button BTN_OPTIONS   = { 20, 250, 200, 40, "OPTIONS" };
const Button BTN_OVL_CONT  = { 20,  70, 200, 42, "CONTINUE" };
const Button BTN_OVL_SPEED = { 20, 125, 200, 42, "SPEED / BPM" };
const Button BTN_OVL_PICK  = { 20, 180, 200, 42, "PICK ANOTHER" };
const Button BTN_OVL_MENU  = { 20, 235, 200, 42, "MAIN MENU" };

// =================== Hit testing ===================
bool inButton(const Button &b, int16_t x, int16_t y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

// =================== Drawing primitives ===================
void drawCenteredText(const char *s, int16_t cx, int16_t cy, uint8_t size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color);
  // Built-in font is 6x8 px per character at size 1; scales linearly.
  int16_t w = (int16_t)strlen(s) * 6 * size;
  int16_t h = 8 * size;
  tft.setCursor(cx - w / 2, cy - h / 2);
  tft.print(s);
}

// Linear interpolation between two RGB565 colours, per channel. t is clamped
// to 0..1. Used by the CLOCK indicator to fade between DIM and ACCENT on
// every clock pulse.
static uint16_t lerpColor565(uint16_t a, uint16_t b, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  int ar = (a >> 11) & 0x1F;
  int ag = (a >> 5)  & 0x3F;
  int ab =  a        & 0x1F;
  int br = (b >> 11) & 0x1F;
  int bg = (b >> 5)  & 0x3F;
  int bb =  b        & 0x1F;
  int r  = ar + (int)((br - ar) * t);
  int g  = ag + (int)((bg - ag) * t);
  int bl = ab + (int)((bb - ab) * t);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

void drawButton(const Button &b, uint16_t bg, uint16_t fg) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, bg);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 8, fg);
  drawCenteredText(b.label, b.x + b.w / 2, b.y + b.h / 2, 2, fg);
}

// =================== Main menu chrome ===================
// drawMenuChrome() paints title strip + buttons on top of the live saver
// framebuffer. No fillScreen, no updateScreen — caller handles the DMA push.
//
// Style: Norns / Teletype / Digitakt-2 — thin opaque title strip, outlined
// buttons (no big colored fills). The screensaver shows through everywhere
// the chrome doesn't actively cover.
void drawMenuChrome() {
  // Title strip — opaque BG bar across the top so particles drifting at the
  // top edge don't speckle the title text.
  tft.fillRect(0, 0, SCREEN_W, TITLE_BAND_H, COLOR_BG);
  tft.drawFastHLine(0, TITLE_BAND_H - 1, SCREEN_W, COLOR_DIVIDER);

  // "Frame" — the project name. Size 2 (was size 3 with a colored band);
  // smaller and on the BG so the look stays minimal.
  drawCenteredText("Frame", SCREEN_W / 2, 18, 2, COLOR_FG);

  // Status line — file count, in dim text.
  char buf[40];
  snprintf(buf, sizeof(buf), "%d gif%s on /gifs", fileCount, fileCount == 1 ? "" : "s");
  drawCenteredText(buf, SCREEN_W / 2, 38, 1, COLOR_DIM);

  // ---- CLOCK indicator (top-right of the title band) ----
  // Shown only when an external eurorack clock is feeding pulses into
  // CLOCK_INPUT_PIN. Two lines of size-1 text, right-aligned:
  //   line 1: "CLOCK"  — colour lerps DIM ↔ ACCENT on every pulse
  //   line 2: "NNNBPM" — current median-smoothed BPM
  // The brightness pulses match the clock's tempo (peak right after each
  // pulse, fade over one beat), so visually the label feels like a tiny
  // blinking LED that keeps time with the source.
  if (clockActive()) {
    float br = clockPulseBrightness();
    uint16_t pulseColor = lerpColor565(COLOR_DIM, COLOR_ACCENT, br);

    // "CLOCK" — 5 chars × 6 px = 30 px wide.
    const int rightPad = 6;
    tft.setTextSize(1);
    tft.setTextColor(pulseColor);
    tft.setCursor(SCREEN_W - rightPad - 30, 8);
    tft.print("CLOCK");

    // BPM number, e.g. "120 BPM". Always-dim so only the label pulses; the
    // number reads as a stable readout under the blinking title.
    char bpmBuf[12];
    float bpm = clockBpm();
    if (bpm >= 1.0f) {
      snprintf(bpmBuf, sizeof(bpmBuf), "%3.0f BPM", bpm);
    } else {
      snprintf(bpmBuf, sizeof(bpmBuf), "-- BPM");
    }
    int textW = (int)strlen(bpmBuf) * 6;
    tft.setTextColor(COLOR_DIM);
    tft.setCursor(SCREEN_W - rightPad - textW, 22);
    tft.print(bpmBuf);
  }

  // Outlined buttons over the saver. Both use COLOR_BG fill + COLOR_FG text
  // (BTN_SELECT) / COLOR_DIM text (BTN_OPTIONS). The outline keeps the saver
  // from leaking into the click target.
  drawButton(BTN_SELECT, COLOR_BG, COLOR_FG);
  drawButton(BTN_OPTIONS, COLOR_BG, COLOR_DIM);
}

// clearMenuChrome() repaints the regions that drawMenuChrome() covers back
// to COLOR_BG. The saver then immediately repaints its own particles in those
// regions on the next stepSaver() call — its STATE is not reset, only the
// crud sitting in the framebuffer is. This is what makes the "chrome melts
// away on idle" effect feel smooth: the saver keeps going, the title and
// buttons just disappear.
void clearMenuChrome() {
  // Title strip.
  tft.fillRect(0, 0, SCREEN_W, TITLE_BAND_H, COLOR_BG);
  // Button regions (slightly wider/taller to also wipe the rounded outline).
  tft.fillRect(BTN_SELECT.x  - 1, BTN_SELECT.y  - 1,
               BTN_SELECT.w  + 2, BTN_SELECT.h  + 2, COLOR_BG);
  tft.fillRect(BTN_OPTIONS.x - 1, BTN_OPTIONS.y - 1,
               BTN_OPTIONS.w + 2, BTN_OPTIONS.h + 2, COLOR_BG);
}

// =================== Options ===================
// Helpers for the two-section, scrollable options screen.
//
// Layout coordinates: each row's logical Y is content-relative (y=0 is the
// top of the first label, just below the header). The viewport Y for a row
// is `HEADER_H + content_y - optionsScrollY`. Rows entirely above HEADER_H
// or below SCREEN_H are skipped; partially-visible rows are drawn and the
// header's fillRect at the end clips anything that scrolled under it.

// Draws a "section caption" — small dim label between sections. Skipped if
// fully off-screen.
static void drawSectionLabel(const char *label, int yViewport) {
  if (yViewport >= SCREEN_H || yViewport + OPTIONS_LABEL_H <= HEADER_H) return;
  tft.setTextColor(COLOR_DIM);
  tft.setTextSize(1);
  // Vertically centre the 8-px text within the OPTIONS_LABEL_H band.
  tft.setCursor(15, yViewport + (OPTIONS_LABEL_H - 8) / 2);
  tft.print(label);
}

// Draws a single option row at viewport-y, height OPTIONS_ITEM_H, with text.
// `isActive` paints it with the highlight bg + accent stripe + accent text.
// Skipped if fully off-screen.
static void drawOptionsRow(int yViewport, const char *label, bool isActive) {
  if (yViewport >= SCREEN_H || yViewport + OPTIONS_ITEM_H <= HEADER_H) return;

  uint16_t rowBg     = isActive ? COLOR_HIGHLIGHT_BG : COLOR_BG;
  uint16_t textColor = isActive ? COLOR_ACCENT       : COLOR_FG;

  tft.fillRect(0, yViewport, SCREEN_W, OPTIONS_ITEM_H, rowBg);
  tft.drawFastHLine(0, yViewport + OPTIONS_ITEM_H - 1, SCREEN_W, COLOR_DIVIDER);

  if (isActive) {
    tft.fillRect(0, yViewport, 4, OPTIONS_ITEM_H, COLOR_ACCENT);
  }

  tft.setTextColor(textColor);
  tft.setTextSize(2);
  // Size-2 text is 16 px tall; centre vertically inside the row.
  tft.setCursor(15, yViewport + (OPTIONS_ITEM_H - 16) / 2);
  tft.print(label);
}

void drawOptions() {
  tft.fillScreen(COLOR_BG);

  const int listTop = HEADER_H;

  // SCREENSAVER section label at content y=0.
  drawSectionLabel("SCREENSAVER", listTop + 0 - optionsScrollY);
  // 6 saver rows starting at content y=OPTIONS_SAVER_TOP.
  for (int i = 0; i < SAVER_COUNT; i++) {
    int contentY = OPTIONS_SAVER_TOP + i * OPTIONS_ITEM_H;
    drawOptionsRow(listTop + contentY - optionsScrollY,
                   saverName((SaverPattern)i),
                   (SaverPattern)i == currentSaver);
  }

  // UI COLOR section label.
  drawSectionLabel("UI COLOR", listTop + OPTIONS_SAVER_BOTTOM - optionsScrollY);
  // 3 UI color rows.
  for (int i = 0; i < UI_COLOR_COUNT; i++) {
    int contentY = OPTIONS_COLOR_TOP + i * OPTIONS_ITEM_H;
    drawOptionsRow(listTop + contentY - optionsScrollY,
                   uiColorName((UiColor)i),
                   (UiColor)i == currentUiColor);
  }

  // Header (drawn last so any rows that scrolled under it get clipped away).
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, COLOR_BG);
  tft.drawFastHLine(0, HEADER_H - 1, SCREEN_W, COLOR_DIVIDER);
  drawCenteredText("OPTIONS", SCREEN_W / 2, HEADER_H / 2, 2, COLOR_FG);
  // Back chevron
  tft.setTextColor(COLOR_FG);
  tft.setTextSize(3);
  tft.setCursor(15, 13);
  tft.print("<");

  tft.updateScreen();
}

// =================== Browser ===================
void drawBrowser() {
  tft.fillScreen(COLOR_BG);

  if (fileCount == 0) {
    drawCenteredText("no gifs found in /gifs", SCREEN_W / 2, SCREEN_H / 2, 1, COLOR_DIM);
  } else {
    int listTop = HEADER_H;
    for (int i = 0; i < fileCount; i++) {
      int y = listTop + i * LIST_ITEM_H - browserScrollY;
      if (y > SCREEN_H) break;
      if (y + LIST_ITEM_H < listTop) continue;

      // Highlight the row that's currently playing (or was last picked).
      bool isPlaying = (playingName.length() > 0 && fileNames[i] == playingName);
      uint16_t rowBg     = isPlaying ? COLOR_HIGHLIGHT_BG : COLOR_BG;
      uint16_t textColor = isPlaying ? COLOR_ACCENT       : COLOR_FG;

      tft.fillRect(0, y, SCREEN_W, LIST_ITEM_H, rowBg);
      tft.drawFastHLine(0, y + LIST_ITEM_H - 1, SCREEN_W, COLOR_DIVIDER);

      // Accent strip on the left edge of the playing row
      if (isPlaying) {
        tft.fillRect(0, y, 4, LIST_ITEM_H, COLOR_ACCENT);
      }

      // Strip extension and trim long names
      String name = fileNames[i];
      int dot = name.lastIndexOf('.');
      if (dot > 0) name = name.substring(0, dot);
      if (name.length() > 18) name = name.substring(0, 17) + "...";

      tft.setTextColor(textColor);
      tft.setTextSize(2);
      tft.setCursor(15, y + (LIST_ITEM_H - 16) / 2);
      tft.print(name);
    }
  }

  // Header (drawn last so list scrolling doesn't bleed into it)
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, COLOR_BG);
  tft.drawFastHLine(0, HEADER_H - 1, SCREEN_W, COLOR_DIVIDER);
  drawCenteredText("GIFS", SCREEN_W / 2, HEADER_H / 2, 2, COLOR_FG);
  // Back chevron
  tft.setTextColor(COLOR_FG);
  tft.setTextSize(3);
  tft.setCursor(15, 13);
  tft.print("<");

  tft.updateScreen();
}

// =================== Overlay ===================
void drawOverlay() {
  // Save the live frame so "Continue" can restore it pixel-perfect.
  memcpy(fbBackup, tft.getFrameBuffer(), SCREEN_W * SCREEN_H * 2);

  // Dim what's behind the overlay
  dimFrameBuffer();

  drawButton(BTN_OVL_CONT,  COLOR_BG, COLOR_FG);
  drawButton(BTN_OVL_SPEED, COLOR_BG, COLOR_ACCENT);
  drawButton(BTN_OVL_PICK,  COLOR_BG, COLOR_FG);
  drawButton(BTN_OVL_MENU,  COLOR_BG, COLOR_FG);

  tft.updateScreen();
}

// Halve every channel of every pixel using a fast 32-bit pass.
// 240*320 = 76800 px = 38400 32-bit words.
void dimFrameBuffer() {
  uint16_t *fb = tft.getFrameBuffer();
  uint32_t *fb32 = (uint32_t *)fb;
  const uint32_t mask = 0x7BEF7BEFu;
  const int n = (SCREEN_W * SCREEN_H) / 2;
  for (int i = 0; i < n; i++) fb32[i] = (fb32[i] >> 1) & mask;
}

void drawSpeedScreen(float baseBpm, float currentBpm, float speedMul) {
  // Compact bottom sheet. The live GIF stays visible above it.
  // Logarithmic slider: 0.5x .. 1.0x .. 2.0x.
  const int sheetY = 190;
  const int sheetH = SCREEN_H - sheetY;

  tft.fillRoundRect(8, sheetY, SCREEN_W - 16, sheetH - 8, 12, COLOR_BG);
  tft.drawRoundRect(8, sheetY, SCREEN_W - 16, sheetH - 8, 12, COLOR_DIVIDER);

  char bpmBuf[24];
  snprintf(bpmBuf, sizeof(bpmBuf), "%.0f BPM", currentBpm);
  drawCenteredText(bpmBuf, SCREEN_W / 2, sheetY + 24, 2, COLOR_ACCENT);

  char subBuf[40];
  snprintf(subBuf, sizeof(subBuf), "original %.0f BPM   %.2fx", baseBpm, speedMul);
  drawCenteredText(subBuf, SCREEN_W / 2, sheetY + 48, 1, COLOR_DIM);

  int railY = SPEED_SLIDER_Y + SPEED_SLIDER_H / 2;
  tft.drawFastHLine(SPEED_SLIDER_X, railY, SPEED_SLIDER_W, COLOR_DIM);

  int centerX = SPEED_SLIDER_X + SPEED_SLIDER_W / 2;
  tft.drawFastVLine(centerX, railY - 13, 27, COLOR_FG);
  drawCenteredText("1x", centerX, railY + 24, 1, COLOR_DIM);

  float pos = logf(speedMul / GIF_SPEED_MIN) / logf(GIF_SPEED_MAX / GIF_SPEED_MIN);
  if (pos < 0.0f) pos = 0.0f;
  if (pos > 1.0f) pos = 1.0f;

  int knobX = SPEED_SLIDER_X + (int)(pos * SPEED_SLIDER_W);

  tft.fillCircle(knobX, railY, 9, COLOR_ACCENT);
  tft.drawCircle(knobX, railY, 11, COLOR_FG);

  // Explicit RESET button. Tap outside the sheet = DONE.
  tft.fillRoundRect(76, 288, 88, 24, 7, COLOR_BG);
  tft.drawRoundRect(76, 288, 88, 24, 7, COLOR_ACCENT);
  drawCenteredText("RESET", SCREEN_W / 2, 300, 1, COLOR_ACCENT);
}
