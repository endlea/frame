/*
  GIF Player UI — Teensy 4.1 + Adafruit 2.8" Capacitive TFT (#2090)
  Display: ILI9341 over SPI
  Touch:   FT6206 over I2C
  Storage: built-in microSD slot of Teensy 4.1

  Portrait orientation (240x320). GIFs come from /gifs on the SD card.
  Tap "SELECT GIF" -> scrollable list of files -> tap a file -> fullscreen playback.
  Tap during playback -> overlay with three buttons: continue / pick another / main menu.

  --- PROJECT LAYOUT ---
  gif_player_ui.ino     entry point: setup, loop, state machine, hardware globals
  config.h              all tunables (pins, screen size, theme, gamma, touch flags, layout)
  ui.h / ui.cpp         buttons, screens (menu/browser/overlay/options), draw primitives
  touch.h / touch.cpp   FT6206 wrapper + tap/drag tracker
  gif_engine.h / .cpp   GIF decoding, gamma correction, SD I/O, file scan
  screensaver.h / .cpp  generative animated background for the main menu

  All files in this folder are auto-compiled together by the Arduino IDE.

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

// Name (basename, e.g. "sailor.gif") of the GIF currently playing or last
// played in this session. Empty = nothing to highlight in the browser.
// Set by enterPlayer() on a successful open, cleared by enterMenu().
String playingName = "";

// Browser scroll. ui.cpp reads browserScrollY when drawing.
int browserScrollY = 0;
int browserMaxScroll = 0;

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
  enterMenu();
}

void loop() {
  updateTouch();

  switch (state) {
    case S_MENU:
      // Animated background — step + redraw chrome on top, throttled.
      if (currentSaver != SAVER_NONE &&
          (millis() - lastSaverFrame) >= SAVER_FRAME_MS) {
        saverStep();
        drawMenuChrome();
        tft.updateScreen();
        lastSaverFrame = millis();
      }
      if (touch.tapped) {
        if (inButton(BTN_SELECT, touch.tapX, touch.tapY)) {
          enterBrowser();
        } else if (inButton(BTN_OPTIONS, touch.tapX, touch.tapY)) {
          enterOptions();
        }
      }
      break;

    case S_BROWSER:
      if (touch.dragged) {
        browserScrollY -= touch.dragDY;
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
      if (touch.tapped) {
        // Back chevron in the header
        if (touch.tapX < BACK_BUTTON_W && touch.tapY < HEADER_H) {
          enterMenu();
        } else if (touch.tapY >= OPTIONS_LIST_TOP) {
          int rel = touch.tapY - OPTIONS_LIST_TOP;
          int idx = rel / LIST_ITEM_H;
          if (idx >= 0 && idx < SAVER_COUNT) {
            currentSaver = (SaverPattern)idx;
            drawOptions();   // refresh highlight; animation kicks in on enterMenu
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

  // Fresh canvas, fresh particles, then chrome on top. The animation loop
  // in S_MENU continues from here.
  tft.fillScreen(COLOR_BG);
  saverInit(currentSaver);
  drawMenuChrome();
  tft.updateScreen();
  lastSaverFrame = millis();
}

static void enterOptions() {
  state = S_OPTIONS;
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