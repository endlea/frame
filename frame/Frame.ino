/*
  Frame — GIF Player on Teensy 4.1 + Adafruit 2.8" Capacitive TFT (#2090)
  Display: ILI9341 over SPI
  Touch:   FT6206 over I2C
  Storage: built-in microSD slot of Teensy 4.1

  Portrait orientation (240x320). GIFs come from /gifs on the SD card.
  Tap "SELECT GIF" -> scrollable list of files -> tap a file -> fullscreen playback.
  Tap during playback -> overlay with three buttons: continue / pick another / main menu.

  Visual style: minimal monochrome (Norns / Teletype / Digitakt 2 vibe). UI
  palette is runtime-switchable in Options → UI COLOR (WHITE / YELLOW / LEGACY).

  --- PROJECT LAYOUT ---
  gif_player_ui.ino       entry point: setup, loop, state machine, hardware globals
  config.h                all tunables (pins, screen size, gamma, touch flags, layout)
  theme.h / theme.cpp     runtime UI palette (3 themes); COLOR_* macros route through it
  ui.h / ui.cpp           buttons, screens (menu/browser/overlay/options), draw primitives
  touch.h / touch.cpp     FT6206 wrapper + tap/drag tracker
  gif_engine.h / .cpp     GIF decoding, gamma correction, SD I/O, file scan
  screensaver.h           public API for the menu's animated background (enum + dispatcher)
  screensavers.cpp        unity-include wrapper at sketch root (see comment inside)
  screensavers/           one .cpp per pattern (bubbles, rain, stars, funny_bubbles, burst)
                          + dispatcher.cpp (wires patterns to the enum)
                          + internal.h (init/step decls) + helpers.h (shared inline utils)

  The Arduino IDE only auto-compiles .cpp at the sketch root and recursively
  under src/. To keep impls in /screensavers/ without an extra src/ wrapper,
  the root-level screensavers.cpp #includes every file in that folder so
  they're built as a single translation unit.

  --- WIRING ---
  Display:
    VIN  -> 5V        (bright backlight; the breakout has its own 3.3V regulator)
    GND  -> GND
    CLK  -> 13        (SCK)
    MISO -> 12
    MOSI -> 11
    CS   -> 10
    D/C  -> 9
    RST  -> 8
    LITE -> not connected

  Capacitive touch (FT6206, I2C):
    SDA  -> 18        (Wire SDA on Teensy 4.1)
    SCL  -> 19        (Wire SCL on Teensy 4.1)
    IRQ  -> not used (polled)

  SD card: built-in microSD slot of the Teensy 4.1.
  Don't connect the breakout's SD pins.

  --- LIBRARIES (Library Manager) ---
  - ILI9341_t3n
  - AnimatedGIF
  - Adafruit_FT6206
  - Adafruit_BusIO (auto-installed)
  - SD (bundled with Teensyduino)

  --- SD CARD ---
  Format: FAT32. Create /gifs in the root and drop .gif files there.
  Generate them with prepare_gif.py using --orientation portrait.
*/

#include <SPI.h>
#include <Wire.h>
#include <SD.h>
#include <ILI9341_t3n.h>
#include <AnimatedGIF.h>
#include <Adafruit_FT6206.h>

#include "config.h"
#include "ui.h"
#include "touch.h"
#include "gif_engine.h"
#include "screensaver.h"
#include "clock_input.h"

// =================== Hardware globals ===================
// These are referenced from other modules via `extern` — see touch.cpp,
// ui.cpp, gif_engine.cpp.
ILI9341_t3n tft = ILI9341_t3n(TFT_CS, TFT_DC, TFT_RST);
Adafruit_FT6206 ts;
AnimatedGIF gif;

// =================== Run-time state ===================
enum AppState { S_MENU, S_BROWSER, S_PLAYER, S_OVERLAY, S_OPTIONS, S_SPEED };
static AppState state = S_MENU;
static String currentFile;
static bool gifOpened = false;
static bool browserReturnToPlayer = false;
static bool speedUiDirty = false;
static uint32_t lastSpeedUiDrawMs = 0;

bool clockSyncEnabled = true;

static bool stableClockActive = false;
static bool prevStableClockActive = false;
static uint32_t clockLostAtMs = 0;
static uint32_t syncToastUntilMs = 0;
static const char *syncToastText = "SYNC";
static bool syncToastDrawn = false;
static uint32_t lastSyncToastDrawMs = 0;

static const float syncRatios[CLOCK_SYNC_RATIO_COUNT] = {
  0.25f, 0.5f, 1.0f, 2.0f, 4.0f
};

static const char *syncRatioLabels[CLOCK_SYNC_RATIO_COUNT] = {
  "/4", "/2", "1x", "2x", "4x"
};

// Not per-GIF. Always resets to default on GIF change / clock loss.
static uint8_t syncRatioIdx = 2;

// Last time the menu animation advanced a frame (millis()).
static uint32_t lastSaverFrame = 0;

// Idle tracking for the main menu. After IDLE_TIMEOUT_MS without a tap, the
// chrome (header + buttons) is hidden so only the screensaver shows. Updated
// on tap in the S_MENU branch and reset by enterMenu().
static uint32_t lastInteractionMs = 0;
// True between "we noticed we went idle and wiped the chrome" and the next
// wake. Used so the active→idle transition does a clean fillScreen+saverInit
// exactly once, instead of every frame while idle.
static bool menuChromeHidden = false;

// Name (basename, e.g. "sailor.gif") of the GIF currently playing or last
// played in this session. Empty = nothing to highlight in the browser.
// Set by enterPlayer() on a successful open, cleared by enterMenu().
String playingName = "";

// Browser scroll. ui.cpp reads browserScrollY when drawing.
int browserScrollY = 0;
int browserMaxScroll = 0;

// Options scroll — same model as the browser. ui.cpp reads optionsScrollY
// when laying out rows; enterOptions() recomputes optionsMaxScroll from the
// total content height vs the visible window.
int optionsScrollY = 0;
int optionsMaxScroll = 0;

// Per-GIF speed state. 1.0x means original GIF timing.
// detectedBpm is learned after the first completed loop of each GIF.
static float gifSpeedMul[MAX_FILES];
static float gifDetectedBpm[MAX_FILES];
static bool gifBpmKnown[MAX_FILES];

static uint32_t gifNextFrameMs = 0;
static uint32_t gifLoopMs = 0;
static int gifLoopFrames = 0;

// Frame backup so the overlay's "Continue" can restore the GIF frame pixel-perfect.
// Sized for full screen at RGB565: 240*320 = 76800 px = 153600 bytes.
DMAMEM uint16_t fbBackup[SCREEN_W * SCREEN_H];

// =================== Forward decls ===================
static void enterMenu();
static void enterBrowser();
static void enterPlayer(const String &name);
static void enterOverlay();
static void exitOverlay();
static void enterOptions();
static void enterSpeed();
static void closeGif();
static void showFatal(const char *msg);

static int currentGifIndex();
static float currentSpeedMul();
static float currentBaseBpm();
static void setCurrentSpeedFromSliderX(int16_t x);
static void resetCurrentSpeed();
static void redrawSpeedUi(bool force);
static bool playGifTimed(bool redrawSpeedUi);
static void restartCurrentGifClean();

static bool shouldUseClockSync();
static float currentSyncRatio();
static const char *currentSyncRatioLabel();
static void stepCurrentSyncRatio(int dir);
static void drawPlayerToastIfNeeded();
static void updateStableClockState();
static void resetSpeedDefaults();

static void forceGifRedrawNoRestart();

// =================== Setup / Loop ===================
void setup() {
  tft.begin(SPI_CLOCK);
  tft.setRotation(ROTATION);
  tft.useFrameBuffer(true);
  tft.fillScreen(COLOR_BG);
  tft.updateScreen();

  Wire.begin();
  if (!ts.begin(40)) showFatal("Touch init failed (FT6206)");
  if (!SD.begin(BUILTIN_SDCARD)) showFatal("SD init failed (Teensy slot)");

  buildGamma();
  gif.begin(LITTLE_ENDIAN_PIXELS);

  scanGifFolder();

  // External eurorack clock input — ISR-driven. See clock_input.cpp for
  // wiring. After this call, clockActive()/clockBpm() reflect live state at
  // any time; we don't have to poll anything.
  initClockInput();

  for (int i = 0; i < MAX_FILES; i++) {
    gifSpeedMul[i] = 1.0f;
    gifDetectedBpm[i] = 120.0f;
    gifBpmKnown[i] = false;
    // gifSyncRatioIdx[i] = 2;  // default = 1x
  }

  // Initialise the saver state ONCE at boot. enterMenu() and the idle/wake
  // transitions deliberately don't call saverInit() any more — the saver's
  // particle state continues across menu re-enters and idle melt-aways, which
  // is what makes the UI feel like the chrome is just a transparent layer
  // sitting on top of an always-running background.
  saverInit(currentSaver);

  enterMenu();
}

void loop() {
  updateTouch();
  updateStableClockState();

  switch (state) {
    case S_MENU: {
      // Has the user been idle long enough that we should hide the chrome?
      bool idle = (millis() - lastInteractionMs) > IDLE_TIMEOUT_MS;

      // Active → idle transition: clear ONLY the chrome regions back to BG.
      // We deliberately do NOT call fillScreen or saverInit — the saver's
      // particle state continues without interruption, and on the very next
      // step its trails will repaint inside the cleared regions. To the eye
      // the chrome simply melts away and the animation keeps going.
      if (idle && !menuChromeHidden) {
        clearMenuChrome();
        tft.updateScreen();
        menuChromeHidden = true;
      }

      // Animated background — step + (if not idle) redraw chrome on top,
      // throttled to SAVER_FRAME_MS.
      if (currentSaver != SAVER_NONE &&
          (millis() - lastSaverFrame) >= SAVER_FRAME_MS) {
        saverStep();
        if (!idle) drawMenuChrome();
        tft.updateScreen();
        lastSaverFrame = millis();
      }

      if (touch.tapped) {
        // We sample `idle` BEFORE updating lastInteractionMs so a tap that
        // arrives during idle still counts as the wake-up tap (and is
        // consumed — it doesn't activate a button).
        lastInteractionMs = millis();
        if (idle) {
          // Wake: redraw the chrome on top of the *existing* saver canvas.
          // No fillScreen, no saverInit — the saver kept running while the
          // chrome was hidden, so its current state is exactly what should
          // appear behind the title and buttons.
          drawMenuChrome();
          tft.updateScreen();
          menuChromeHidden = false;
        } else if (inButton(BTN_SELECT, touch.tapX, touch.tapY)) {
          enterBrowser();
        } else if (inButton(BTN_OPTIONS, touch.tapX, touch.tapY)) {
          enterOptions();
        }
      }
      break;
    }

    case S_BROWSER:
      if (touch.dragged) {
        int dy = constrain(touch.dragDY, -36, 36);

        browserScrollY -= dy;
        if (browserScrollY < 0) browserScrollY = 0;
        if (browserScrollY > browserMaxScroll) browserScrollY = browserMaxScroll;

        drawBrowser();
      } else if (touch.tapped) {
        // Back chevron in the header
        if (touch.tapX < BACK_BUTTON_W && touch.tapY < HEADER_H) {
          if (browserReturnToPlayer && gifOpened) {
            browserReturnToPlayer = false;
            state = S_PLAYER;

            // Browser overwrote the framebuffer. Restore the last clean GIF frame
            // instead of clearing to BG, otherwise transparent GIFs rebuild in chunks.
            memcpy(tft.getFrameBuffer(), fbBackup, SCREEN_W * SCREEN_H * 2);
            tft.updateScreen();

            gifNextFrameMs = millis();
          } else {
            browserReturnToPlayer = false;
            enterMenu();
          }
        } else if (touch.tapY >= HEADER_H && fileCount > 0) {
          int rel = touch.tapY - HEADER_H + browserScrollY;
          int idx = rel / LIST_ITEM_H;
          if (idx >= 0 && idx < fileCount) {
            browserReturnToPlayer = false;
            enterPlayer(fileNames[idx]);
          }
        }
      }
      break;

    case S_PLAYER:
      if (gifOpened) {
        playGifTimed(false);
        memcpy(fbBackup, tft.getFrameBuffer(), SCREEN_W * SCREEN_H * 2);
        drawPlayerToastIfNeeded();
      }
      if (touch.tapped) enterOverlay();
      break;

    case S_OVERLAY:
      if (touch.tapped) {
        if (inButton(BTN_OVL_CONT, touch.tapX, touch.tapY)) {
          exitOverlay();
        } else if (inButton(BTN_OVL_SPEED, touch.tapX, touch.tapY)) {
          enterSpeed();
        } else if (inButton(BTN_OVL_PICK, touch.tapX, touch.tapY)) {
          browserReturnToPlayer = true;
          enterBrowser();
        } else if (inButton(BTN_OVL_MENU, touch.tapX, touch.tapY)) {
          browserReturnToPlayer = false;
          closeGif();
          enterMenu();
        }
      }
      break;

    case S_OPTIONS:
      if (touch.dragged) {
        int dy = constrain(touch.dragDY, -36, 36);

        optionsScrollY -= dy;
        if (optionsScrollY < 0) optionsScrollY = 0;
        if (optionsScrollY > optionsMaxScroll) optionsScrollY = optionsMaxScroll;

        drawOptions();
      } else if (touch.tapped) {
        // Back chevron in the header
        if (touch.tapX < BACK_BUTTON_W && touch.tapY < HEADER_H) {
          enterMenu();
        } else if (touch.tapY >= HEADER_H) {
          int contentY = touch.tapY - HEADER_H + optionsScrollY;

          // Ignore taps on section label bands.
          if (contentY < OPTIONS_SAVER_TOP) {
            break;
          }

          // SCREENSAVER section
          if (contentY >= OPTIONS_SAVER_TOP && contentY < OPTIONS_SAVER_BOTTOM) {
            int rowOffset = contentY - OPTIONS_SAVER_TOP;

            // Extra safety: ignore taps exactly on row borders.
            if ((rowOffset % OPTIONS_ITEM_H) < 4 || (rowOffset % OPTIONS_ITEM_H) > OPTIONS_ITEM_H - 4) {
              break;
            }

            int idx = rowOffset / OPTIONS_ITEM_H;

            if (idx >= 0 && idx < SAVER_COUNT && (SaverPattern)idx != currentSaver) {
              saverInit((SaverPattern)idx);
              drawOptions();
            }

            break;
          }

          // Ignore UI COLOR label band.
          if (contentY >= OPTIONS_SAVER_BOTTOM && contentY < OPTIONS_COLOR_TOP) {
            break;
          }

          // UI COLOR section
          if (contentY >= OPTIONS_COLOR_TOP && contentY < OPTIONS_COLOR_BOTTOM) {
            int rowOffset = contentY - OPTIONS_COLOR_TOP;

            // Extra safety: ignore taps exactly on row borders.
            if ((rowOffset % OPTIONS_ITEM_H) < 4 || (rowOffset % OPTIONS_ITEM_H) > OPTIONS_ITEM_H - 4) {
              break;
            }

            int idx = rowOffset / OPTIONS_ITEM_H;

            if (idx >= 0 && idx < UI_COLOR_COUNT && (UiColor)idx != currentUiColor) {
              currentUiColor = (UiColor)idx;
              drawOptions();
            }
          }

          // CLOCK section
          if (contentY >= OPTIONS_CLOCK_TOP && contentY < OPTIONS_CLOCK_BOTTOM) {
            clockSyncEnabled = !clockSyncEnabled;

            // Turning clock sync off returns speed/ratio to defaults.
            if (!clockSyncEnabled) {
              syncRatioIdx = 2;
            }

            drawOptions();
          }
        }
      }
      break;

    case S_SPEED:
      if (gifOpened) {
        playGifTimed(true);
      }

      if (shouldUseClockSync()) {
        if (touch.tapped) {
          // Ratio left arrow.
          if (touch.tapY >= 244 && touch.tapY <= 286 && touch.tapX >= 36 && touch.tapX < 84) {
            stepCurrentSyncRatio(-1);
            speedUiDirty = true;
            redrawSpeedUi(true);
          }
          // Ratio right arrow.
          else if (touch.tapY >= 244 && touch.tapY <= 286 && touch.tapX > 156 && touch.tapX <= 204) {
            stepCurrentSyncRatio(1);
            speedUiDirty = true;
            redrawSpeedUi(true);
          }
          // Any tap outside ratio controls = DONE.
          else {
            state = S_PLAYER;
            speedUiDirty = false;
            forceGifRedrawNoRestart();
            break;
          }
        }
      } else {
        if (touch.dragged) {
          if (touch.curY >= SPEED_SLIDER_Y - 28 &&
              touch.curY <= SPEED_SLIDER_Y + SPEED_SLIDER_H + 28) {
            setCurrentSpeedFromSliderX(touch.curX);
            speedUiDirty = true;
          }
        } else if (touch.tapped) {
          // RESET button.
          if (touch.tapX >= 76 && touch.tapX < 164 &&
              touch.tapY >= 288 && touch.tapY < 312) {
            resetCurrentSpeed();
            speedUiDirty = true;
            redrawSpeedUi(true);
          }
          // Slider tap.
          else if (touch.tapY >= SPEED_SLIDER_Y - 28 &&
                   touch.tapY <= SPEED_SLIDER_Y + SPEED_SLIDER_H + 28) {
            setCurrentSpeedFromSliderX(touch.tapX);
            speedUiDirty = true;
            redrawSpeedUi(true);
          }
          // Any tap outside the speed controls = DONE.
          else {
            state = S_PLAYER;
            speedUiDirty = false;
            forceGifRedrawNoRestart();
            break;
          }
        }
      }

      redrawSpeedUi(false);
      break;
  }
}

// =================== State transitions ===================
static void enterMenu() {
  state = S_MENU;
  playingName = "";   // forget the highlight when returning to the main menu

  // Reset the idle clock — entering the menu is itself an interaction.
  lastInteractionMs = millis();
  menuChromeHidden = false;

  // Fresh canvas + chrome. We DO fillScreen here because we're coming back
  // from another full-screen view (browser / options / player) which
  // overwrote the framebuffer entirely — there's no preserved saver state
  // worth keeping in the buffer. We do NOT call saverInit, though: the
  // saver's internal particle state still holds the field from before the
  // user left, so the next saverStep() will repaint a coherent frame on
  // top of the fresh BG instead of starting over from random positions.
  tft.fillScreen(COLOR_BG);
  drawMenuChrome();
  tft.updateScreen();
  lastSaverFrame = millis();
}

static void enterOptions() {
  state = S_OPTIONS;
  // Compute scroll bounds from content height vs visible window. If the
  // content fits in one screen, maxScroll stays 0 and the screen behaves
  // like the old non-scrolling version.
  int listH = SCREEN_H - HEADER_H;
  optionsMaxScroll = max(0, OPTIONS_CONTENT_H - listH);
  if (optionsScrollY > optionsMaxScroll) optionsScrollY = optionsMaxScroll;
  if (optionsScrollY < 0) optionsScrollY = 0;
  drawOptions();
}

static void enterBrowser() {
  state = S_BROWSER;
  int contentH = fileCount * LIST_ITEM_H;
  int listH = SCREEN_H - HEADER_H;
  browserMaxScroll = max(0, contentH - listH);
  if (browserScrollY > browserMaxScroll) browserScrollY = browserMaxScroll;
  drawBrowser();
}

static void enterPlayer(const String &name) {
  state = S_PLAYER;
  closeGif();
  currentFile = String(GIF_DIR) + "/" + name;
  if (gif.open(currentFile.c_str(), GIFOpen, GIFClose, GIFRead, GIFSeek, GIFDraw)) {
    gifOpened = true;
    playingName = name;     // remember which row to highlight in the browser

    // No per-GIF speed memory. Every selected GIF starts from defaults.
    resetSpeedDefaults();

    gifNextFrameMs = 0;
    gifLoopMs = 0;
    gifLoopFrames = 0;

    tft.fillScreen(COLOR_BG);
    tft.updateScreen();
  } else {
    tft.fillScreen(COLOR_BG);
    drawCenteredText("Failed to open GIF", SCREEN_W / 2, SCREEN_H / 2, 2, ILI9341_RED);
    tft.updateScreen();
    delay(1500);
    enterBrowser();
  }
}

static void enterOverlay() {
  state = S_OVERLAY;
  drawOverlay();
}

static void exitOverlay() {
  state = S_PLAYER;
  // Restore the saved pre-overlay frame
  memcpy(tft.getFrameBuffer(), fbBackup, SCREEN_W * SCREEN_H * 2);
  tft.updateScreen();

  // Keep timing smooth after leaving overlay instead of trying to "catch up"
  // with frames that elapsed while the overlay was open.
  gifNextFrameMs = millis();
}

static void enterSpeed() {
  state = S_SPEED;

  // Restore the clean GIF frame first, removing the dimmed overlay buttons.
  memcpy(tft.getFrameBuffer(), fbBackup, SCREEN_W * SCREEN_H * 2);

  // Save this clean frame again so exiting speed can restore it instantly.
  memcpy(fbBackup, tft.getFrameBuffer(), SCREEN_W * SCREEN_H * 2);

  speedUiDirty = true;
  redrawSpeedUi(true);

  // Don't burst through delayed frames after switching UI state.
  gifNextFrameMs = millis();
}

static void closeGif() {
  if (gifOpened) {
    gif.close();
    gifOpened = false;
  }
  closeGifFile();

  gifNextFrameMs = 0;
  gifLoopMs = 0;
  gifLoopFrames = 0;
}

static void showFatal(const char *msg) {
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(ILI9341_RED);
  tft.setTextSize(2);
  tft.setCursor(10, SCREEN_H / 2 - 16);
  tft.println(msg);
  tft.updateScreen();
  while (1) {}
}

// =================== GIF speed / BPM helpers ===================
static int currentGifIndex() {
  for (int i = 0; i < fileCount; i++) {
    if (fileNames[i] == playingName) return i;
  }
  return -1;
}

static float currentSpeedMul() {
  int idx = currentGifIndex();
  if (idx < 0) return 1.0f;

  if (shouldUseClockSync()) {
    float base = currentBaseBpm();
    if (base < 1.0f) base = 120.0f;

    float mul = (clockBpm() * currentSyncRatio()) / base;

    if (mul < GIF_SPEED_MIN) mul = GIF_SPEED_MIN;
    if (mul > GIF_SPEED_MAX) mul = GIF_SPEED_MAX;

    return mul;
  }

  if (gifSpeedMul[idx] < GIF_SPEED_MIN) gifSpeedMul[idx] = GIF_SPEED_MIN;
  if (gifSpeedMul[idx] > GIF_SPEED_MAX) gifSpeedMul[idx] = GIF_SPEED_MAX;

  return gifSpeedMul[idx];
}

static float currentBaseBpm() {
  int idx = currentGifIndex();
  if (idx < 0) return 120.0f;
  return gifDetectedBpm[idx];
}

static void setCurrentSpeedFromSliderX(int16_t x) {
  int idx = currentGifIndex();
  if (idx < 0) return;

  float pos = (float)(x - SPEED_SLIDER_X) / (float)SPEED_SLIDER_W;
  if (pos < 0.0f) pos = 0.0f;
  if (pos > 1.0f) pos = 1.0f;

  // left = 0.5x, center = 1.0x, right = 2.0x.
  float mul = GIF_SPEED_MIN * powf(GIF_SPEED_MAX / GIF_SPEED_MIN, pos);

  gifSpeedMul[idx] = mul;
}

static void resetCurrentSpeed() {
  int idx = currentGifIndex();
  if (idx >= 0) gifSpeedMul[idx] = 1.0f;
}

static void redrawSpeedUi(bool force) {
  if (!force && !speedUiDirty) return;

  uint32_t now = millis();

  if (!force && (now - lastSpeedUiDrawMs) < 45) return;

  memcpy(tft.getFrameBuffer(), fbBackup, SCREEN_W * SCREEN_H * 2);

  if (shouldUseClockSync()) {
    drawClockSpeedScreen(currentBaseBpm(),
                         clockBpm(),
                         currentSyncRatio(),
                         currentSyncRatioLabel());
  } else {
    drawSpeedScreen(currentBaseBpm(),
                    currentBaseBpm() * currentSpeedMul(),
                    currentSpeedMul());
  }

  tft.updateScreen();

  speedUiDirty = false;
  lastSpeedUiDrawMs = now;
}

static bool playGifTimed(bool redrawSpeedUiAfterFrame) {
  if (!gifOpened) return false;

  uint32_t now = millis();
  if (gifNextFrameMs != 0 && (int32_t)(now - gifNextFrameMs) < 0) {
    return true;
  }

  // IMPORTANT:
  // In speed UI mode the framebuffer currently contains the bottom sheet.
  // Transparent / partial GIF frames would otherwise decode over that UI and
  // bake its pixels into the animation. Restore the last clean GIF frame first.
  if (redrawSpeedUiAfterFrame) {
    memcpy(tft.getFrameBuffer(), fbBackup, SCREEN_W * SCREEN_H * 2);
  }

  int frameDelayMs = 0;
  int playRet = gif.playFrame(false, &frameDelayMs);

  if (playRet > 0) {
    if (frameDelayMs <= 0) frameDelayMs = 33;

    gifLoopMs += frameDelayMs;
    gifLoopFrames++;

    float mul = currentSpeedMul();
    int scaledDelay = (int)((float)frameDelayMs / mul);
    if (scaledDelay < 5) scaledDelay = 5;

    gifNextFrameMs = millis() + scaledDelay;

    if (redrawSpeedUiAfterFrame) {
      // Save clean GIF frame BEFORE drawing speed UI.
      memcpy(fbBackup, tft.getFrameBuffer(), SCREEN_W * SCREEN_H * 2);

      if (shouldUseClockSync()) {
        drawClockSpeedScreen(currentBaseBpm(),
                             clockBpm(),
                             currentSyncRatio(),
                             currentSyncRatioLabel());
      } else {
        drawSpeedScreen(currentBaseBpm(),
                        currentBaseBpm() * currentSpeedMul(),
                        currentSpeedMul());
      }

      tft.updateScreen();

      speedUiDirty = false;
      lastSpeedUiDrawMs = millis();
    } else {
      if (syncToastUntilMs != 0 && (int32_t)(millis() - syncToastUntilMs) < 0) {
        drawSyncToast(syncToastText);
      }

      tft.updateScreen();
    }

    return true;
  }

  // EOF or error -> loop the GIF.
  int idx = currentGifIndex();

  // Learn original BPM from the first complete loop.
  // Assumption: one GIF loop = one 4/4 bar = 4 beats.
  if (idx >= 0 && !gifBpmKnown[idx] && gifLoopMs > 0 && gifLoopFrames > 1) {
    gifDetectedBpm[idx] = 240000.0f / (float)gifLoopMs;
    if (gifDetectedBpm[idx] < 20.0f) gifDetectedBpm[idx] = 20.0f;
    if (gifDetectedBpm[idx] > 300.0f) gifDetectedBpm[idx] = 300.0f;
    gifBpmKnown[idx] = true;
  }

  gif.close();
  gifOpened = false;

  gifLoopMs = 0;
  gifLoopFrames = 0;
  gifNextFrameMs = 0;

  if (gif.open(currentFile.c_str(), GIFOpen, GIFClose, GIFRead, GIFSeek, GIFDraw)) {
    gifOpened = true;
    return true;
  }

  enterBrowser();
  return false;
}

static void restartCurrentGifClean() {
  if (gifOpened) {
    gif.close();
    gifOpened = false;
  }
  closeGifFile();

  tft.fillScreen(COLOR_BG);
  tft.updateScreen();

  gifNextFrameMs = 0;
  gifLoopMs = 0;
  gifLoopFrames = 0;

  if (gif.open(currentFile.c_str(), GIFOpen, GIFClose, GIFRead, GIFSeek, GIFDraw)) {
    gifOpened = true;

    // Immediately draw first frame into a clean canvas.
    int frameDelayMs = 0;
    int playRet = gif.playFrame(false, &frameDelayMs);

    if (playRet > 0) {
      if (frameDelayMs <= 0) frameDelayMs = 33;

      float mul = currentSpeedMul();
      int scaledDelay = (int)((float)frameDelayMs / mul);
      if (scaledDelay < 5) scaledDelay = 5;

      gifNextFrameMs = millis() + scaledDelay;
      tft.updateScreen();
    } else {
      gifNextFrameMs = millis();
    }
  }
}

static bool shouldUseClockSync() {
  return clockSyncEnabled && stableClockActive;
}

static float currentSyncRatio() {
  if (syncRatioIdx >= CLOCK_SYNC_RATIO_COUNT) syncRatioIdx = 2;
  return syncRatios[syncRatioIdx];
}

static const char *currentSyncRatioLabel() {
  if (syncRatioIdx >= CLOCK_SYNC_RATIO_COUNT) syncRatioIdx = 2;
  return syncRatioLabels[syncRatioIdx];
}

static void stepCurrentSyncRatio(int dir) {
  int r = syncRatioIdx;
  r += dir;

  if (r < 0) r = 0;
  if (r >= CLOCK_SYNC_RATIO_COUNT) r = CLOCK_SYNC_RATIO_COUNT - 1;

  syncRatioIdx = (uint8_t)r;
}

static void drawPlayerToastIfNeeded() {
  if (syncToastUntilMs == 0) return;

  if ((int32_t)(millis() - syncToastUntilMs) >= 0) {
    syncToastUntilMs = 0;
    syncToastDrawn = false;
    lastSyncToastDrawMs = 0;
  }
}

static void updateStableClockState() {
  bool rawClock = clockActive();

  if (rawClock) {
    stableClockActive = true;
    clockLostAtMs = 0;
  } else if (stableClockActive) {
    if (clockLostAtMs == 0) clockLostAtMs = millis();

    // Hysteresis: don't drop sync UI on one missed/late pulse.
    if (millis() - clockLostAtMs > 250) {
      stableClockActive = false;
      clockLostAtMs = 0;

      syncRatioIdx = 2;

      if (clockSyncEnabled && state == S_PLAYER) {
        syncToastText = "SYNC OFF";
        syncToastUntilMs = millis() + 1600;
        syncToastDrawn = false;
        lastSyncToastDrawMs = 0;
      }

      if (state == S_SPEED) {
        speedUiDirty = true;
        redrawSpeedUi(true);
      }
    }
  }

  if (clockSyncEnabled && stableClockActive && !prevStableClockActive && state == S_PLAYER) {
    syncToastText = "SYNC ON";
    syncToastUntilMs = millis() + 1600;
    syncToastDrawn = false;
    lastSyncToastDrawMs = 0;
  }

  prevStableClockActive = stableClockActive;
}

static void resetSpeedDefaults() {
  int idx = currentGifIndex();

  if (idx >= 0) {
    gifSpeedMul[idx] = 1.0f;
  }

  syncRatioIdx = 2;
}

static void forceGifRedrawNoRestart() {
  // Restore latest clean GIF frame saved before drawing the speed sheet.
  // No fillScreen/fillRect here — bright GIFs show black flashes otherwise.
  memcpy(tft.getFrameBuffer(), fbBackup, SCREEN_W * SCREEN_H * 2);
  tft.updateScreen();

  speedUiDirty = false;
  gifNextFrameMs = millis();
}
