// gif_engine.h — GIF decoding glue: gamma correction, AnimatedGIF render callback,
// SD-backed file callbacks, and the SD scan that builds the file list.
#pragma once
#include <Arduino.h>
#include <AnimatedGIF.h>
#include <SD.h>
#include "config.h"

// Catalogue of .gif files in GIF_DIR, populated by scanGifFolder().
extern String fileNames[MAX_FILES];
extern int fileCount;

// Build the gamma + white-balance lookup tables (call once in setup).
void buildGamma();

// Apply gamma + white balance to a single RGB565 pixel.
uint16_t applyGamma(uint16_t c);

// AnimatedGIF callbacks — pass these to gif.open(...).
void GIFDraw(GIFDRAW *pDraw);
void *GIFOpen(const char *fname, int32_t *pSize);
void GIFClose(void *handle);
int32_t GIFRead(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen);
int32_t GIFSeek(GIFFILE *pFile, int32_t iPosition);

// Re-scan the SD GIF folder, populating fileNames[] / fileCount.
void scanGifFolder();

// Defensively close the SD::File backing the currently-playing GIF.
// Normally gif.close() already does this through GIFClose(); use this only
// if the gif state is unknown.
void closeGifFile();
