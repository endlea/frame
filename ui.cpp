#include "ui.h"
#include "gif_engine.h"     // fileNames, fileCount
#include "screensaver.h"    // currentSaver, saverName, SAVER_COUNT
#include <ILI9341_t3n.h>

// `tft` is defined in gif_player_ui.ino.
extern ILI9341_t3n tft;

// `fbBackup`, `browserScrollY` and `playingName` are owned by gif_player_ui.ino.
// drawOverlay() copies the live frame into fbBackup so "Continue" can restore it.
// drawBrowser() reads browserScrollY for list position, and playingName to
// know which row should be marked as "now playing" (empty = no highlight).
extern uint16_t fbBackup[];
extern int browserScrollY;
extern String playingName;

// =================== Button instances ===================
const Button BTN_SELECT   = { 20, 175, 200, 60, "SELECT GIF" };
const Button BTN_OPTIONS  = { 20, 250, 200, 40, "OPTIONS" };
const Button BTN_OVL_CONT = { 20,  90, 200, 60, "CONTINUE" };
const Button BTN_OVL_PICK = { 20, 160, 200, 60, "PICK ANOTHER" };
const Button BTN_OVL_MENU = { 20, 230, 200, 60, "MAIN MENU" };

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

void drawButton(const Button &b, uint16_t bg, uint16_t fg) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, bg);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 8, fg);
  drawCenteredText(b.label, b.x + b.w / 2, b.y + b.h / 2, 2, fg);
}

// =================== Screens ===================
// Draws everything that should sit *on top* of the animated background:
// header band, title, status, buttons. No fillScreen — that would clobber the
// saver. No updateScreen — the caller batches the DMA push.
void drawMenuChrome() {
  // Header band — opaque, hides any particles drifting underneath
  tft.fillRect(0, 0, SCREEN_W, 100, COLOR_HEADER);
  drawCenteredText("GIF PLAYER", SCREEN_W / 2, 50, 3, COLOR_ACCENT);

  // Subtitle / status — these are text-only over the animation; particles
  // can drift around the glyphs and that's intentional.
  drawCenteredText("teensy 4.1 + ili9341", SCREEN_W / 2, 130, 1, COLOR_DIM);
  char buf[40];
  snprintf(buf, sizeof(buf), "%d gif%s on /gifs", fileCount, fileCount == 1 ? "" : "s");
  drawCenteredText(buf, SCREEN_W / 2, 150, 1, COLOR_DIM);

  // Primary button (filled accent)
  drawButton(BTN_SELECT, COLOR_BUTTON, COLOR_BUTTON_TXT);
  // Secondary button (outlined, dim)
  drawButton(BTN_OPTIONS, COLOR_BG, COLOR_DIM);
}

void drawOptions() {
  tft.fillScreen(COLOR_BG);

  // Section label
  tft.setTextColor(COLOR_DIM);
  tft.setTextSize(1);
  tft.setCursor(15, HEADER_H + 20);
  tft.print("BACKGROUND");

  // Pattern list — same highlight style as the GIF browser
  for (int i = 0; i < SAVER_COUNT; i++) {
    int y = OPTIONS_LIST_TOP + i * LIST_ITEM_H;
    bool isActive = ((SaverPattern)i == currentSaver);

    uint16_t rowBg     = isActive ? COLOR_HIGHLIGHT_BG : COLOR_BG;
    uint16_t textColor = isActive ? COLOR_ACCENT       : COLOR_FG;

    tft.fillRect(0, y, SCREEN_W, LIST_ITEM_H, rowBg);
    tft.drawFastHLine(0, y + LIST_ITEM_H - 1, SCREEN_W, COLOR_DIVIDER);

    if (isActive) {
      tft.fillRect(0, y, 4, LIST_ITEM_H, COLOR_ACCENT);
    }

    tft.setTextColor(textColor);
    tft.setTextSize(2);
    tft.setCursor(15, y + (LIST_ITEM_H - 16) / 2);
    tft.print(saverName((SaverPattern)i));
  }

  // Header (drawn last so list rendering doesn't bleed into it)
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, COLOR_HEADER);
  drawCenteredText("OPTIONS", SCREEN_W / 2, HEADER_H / 2, 2, COLOR_FG);
  tft.setTextColor(COLOR_ACCENT);
  tft.setTextSize(3);
  tft.setCursor(15, 13);
  tft.print("<");
  tft.drawFastHLine(0, HEADER_H, SCREEN_W, COLOR_DIVIDER);

  tft.updateScreen();
}

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
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, COLOR_HEADER);
  drawCenteredText("GIFS", SCREEN_W / 2, HEADER_H / 2, 2, COLOR_FG);
  // Back chevron
  tft.setTextColor(COLOR_ACCENT);
  tft.setTextSize(3);
  tft.setCursor(15, 13);
  tft.print("<");
  tft.drawFastHLine(0, HEADER_H, SCREEN_W, COLOR_DIVIDER);

  tft.updateScreen();
}

void drawOverlay() {
  // Save the live frame so "Continue" can restore it pixel-perfect.
  memcpy(fbBackup, tft.getFrameBuffer(), SCREEN_W * SCREEN_H * 2);

  // Dim what's behind the overlay
  dimFrameBuffer();

  // Three buttons
  drawButton(BTN_OVL_CONT, COLOR_BUTTON, COLOR_BUTTON_TXT);
  drawButton(BTN_OVL_PICK, COLOR_BUTTON, COLOR_BUTTON_TXT);
  drawButton(BTN_OVL_MENU, COLOR_BUTTON, COLOR_BUTTON_TXT);

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