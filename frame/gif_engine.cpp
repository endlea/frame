#include "gif_engine.h"
#include <ILI9341_t3n.h>

// `tft` is defined in gif_player_ui.ino.
extern ILI9341_t3n tft;

// Catalogue (definitions live here, declarations in gif_engine.h).
String fileNames[MAX_FILES];
int fileCount = 0;

// Gamma lookup tables: R and B are 5-bit (32 levels), G is 6-bit (64 levels).
static uint8_t gammaR[32], gammaG[64], gammaB[32];

// SD::File handle backing the GIF currently being read.
// Returned by GIFOpen, used by GIFRead/GIFSeek, closed by GIFClose.
static File gifFile;

static inline float clamp01(float v) {
  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void buildGamma() {
  for (int i = 0; i < 32; i++) {
    float v = powf(i / 31.0f, GAMMA);
    gammaR[i] = (uint8_t)(clamp01(v * R_GAIN) * 31.0f + 0.5f);
    gammaB[i] = (uint8_t)(clamp01(v * B_GAIN) * 31.0f + 0.5f);
  }
  for (int i = 0; i < 64; i++) {
    float v = powf(i / 63.0f, GAMMA);
    gammaG[i] = (uint8_t)(clamp01(v * G_GAIN) * 63.0f + 0.5f);
  }
}

uint16_t applyGamma(uint16_t c) {
  uint8_t r = (c >> 11) & 0x1F;
  uint8_t g = (c >> 5)  & 0x3F;
  uint8_t b =  c        & 0x1F;
  return (uint16_t(gammaR[r]) << 11) | (uint16_t(gammaG[g]) << 5) | uint16_t(gammaB[b]);
}

// Per-line draw callback — runs once per scanline of the decoded GIF.
void GIFDraw(GIFDRAW *pDraw) {
  uint16_t lineBuf[SCREEN_W];
  uint8_t *src  = pDraw->pPixels;
  uint16_t *pal = pDraw->pPalette;
  int w = pDraw->iWidth;
  if (w > SCREEN_W) w = SCREEN_W;
  int y = pDraw->iY + pDraw->y;

  if (pDraw->ucHasTransparency) {
    // Draw only the non-transparent runs; transparent regions keep the previous frame.
    uint8_t transparent = pDraw->ucTransparent;
    int x = 0;
    while (x < w) {
      while (x < w && src[x] == transparent) x++;
      if (x >= w) break;
      int runStart = x;
      int n = 0;
      while (x < w && src[x] != transparent) {
        lineBuf[n++] = applyGamma(pal[src[x++]]);
      }
      tft.writeRect(pDraw->iX + runStart, y, n, 1, lineBuf);
    }
  } else {
    for (int x = 0; x < w; x++) lineBuf[x] = applyGamma(pal[src[x]]);
    tft.writeRect(pDraw->iX, y, w, 1, lineBuf);
  }
}

// ---- SD-backed file callbacks for AnimatedGIF ----
void *GIFOpen(const char *fname, int32_t *pSize) {
  gifFile = SD.open(fname);
  if (gifFile) {
    *pSize = gifFile.size();
    return (void *)&gifFile;
  }
  return NULL;
}

void GIFClose(void *handle) {
  if (gifFile) gifFile.close();
}

int32_t GIFRead(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  int32_t iBytesRead = iLen;
  File *f = (File *)pFile->fHandle;
  if ((pFile->iSize - pFile->iPos) < iLen) iBytesRead = pFile->iSize - pFile->iPos - 1;
  if (iBytesRead <= 0) return 0;
  iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
  pFile->iPos = f->position();
  return iBytesRead;
}

int32_t GIFSeek(GIFFILE *pFile, int32_t iPosition) {
  File *f = (File *)pFile->fHandle;
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  return iPosition;
}

void scanGifFolder() {
  fileCount = 0;
  File root = SD.open(GIF_DIR);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }
  File entry;
  while ((entry = root.openNextFile())) {
    if (!entry.isDirectory()) {
      const char *n = entry.name();
      int len = strlen(n);
      // Skip dotfiles (._foo.gif and friends — macOS metadata on FAT32, .DS_Store, etc.)
      if (n[0] != '.' &&
          len > 4 &&
          strcasecmp(n + len - 4, ".gif") == 0 &&
          fileCount < MAX_FILES) {
        fileNames[fileCount++] = String(n);
      }
    }
    entry.close();
  }
  root.close();

  // Insertion sort, alphabetical
  for (int i = 1; i < fileCount; i++) {
    String tmp = fileNames[i];
    int j = i - 1;
    while (j >= 0 && fileNames[j].compareTo(tmp) > 0) {
      fileNames[j + 1] = fileNames[j];
      j--;
    }
    fileNames[j + 1] = tmp;
  }
}

void closeGifFile() {
  if (gifFile) gifFile.close();
}
