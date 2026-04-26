/*
  Frames — GIF Player on Teensy 4.1 + Adafruit 2.8" Capacitive TFT (#2090)
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

// =================== Hardware globals ===================
// These are referenced from other modules via `extern` — see touch.cpp,
// ui.cpp, gif_engine.cpp.
ILI9341_t3n tft = ILI9341_t3n(TFT_CS, TFT_DC, TFT_RST);
Adafruit_FT6206 ts;
AnimatedGIF gif;

// =================== Run-time state ===================
enum AppState { S_MENU, S_BROWSER, S_PLAYER, S_OVERLAY, S_OPTIONS };
static AppState state = S_MENU;
static String currentFile;
static bool gifOpened = false;

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
static void closeGif();
static void showFatal(const char *msg);

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
          enterMenu();
        } else if (touch.tapY >= HEADER_H && fileCount > 0) {
          int rel = touch.tapY - HEADER_H + browserScrollY;
          int idx = rel / LIST_ITEM_H;
          if (idx >= 0 && idx < fileCount) {
            enterPlayer(fileNames[idx]);
          }
        }
      }
      break;

    case S_PLAYER:
      if (gifOpened) {
        int playRet = gif.playFrame(false, NULL);
        tft.updateScreen();
        if (playRet <= 0) {
          // EOF or error -> loop the GIF
          gif.close();
          gifOpened = false;
          if (gif.open(currentFile.c_str(), GIFOpen, GIFClose, GIFRead, GIFSeek, GIFDraw)) {
            gifOpened = true;
          } else {
            enterBrowser();
            break;
          }
        }
      }
      if (touch.tapped) enterOverlay();
      break;

    case S_OVERLAY:
      if (touch.tapped) {
        if (inButton(BTN_OVL_CONT, touch.tapX, touch.tapY)) {
          exitOverlay();
        } else if (inButton(BTN_OVL_PICK, touch.tapX, touch.tapY)) {
          closeGif();
          enterBrowser();
        } else if (inButton(BTN_OVL_MENU, touch.tapX, touch.tapY)) {
          closeGif();
          enterMenu();
        }
      }
      break;

    case S_OPTIONS:
      if (touch.dragged) {
        int dy = constrain(touch.dragDY, -18, 18);

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
        }
      }
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
}

static void closeGif() {
  if (gifOpened) {
    gif.close();
    gifOpened = false;
  }
  closeGifFile();
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
