// internal.h — private header shared between dispatcher.cpp and the
// individual pattern .cpp files in /screensavers/. NOT a public header.
//
// Each pattern exposes exactly two functions:
//   initFoo()  — reset particle/state arrays
//   stepFoo()  — draw one frame into the framebuffer
// Dispatcher routes the active enum to the right pair.
#pragma once
#include <Arduino.h>

void initBubbles();        void stepBubbles();         // ER-301 style pulsing
void initRain();           void stepRain();            // pixel rain (Matrix-ish)
void initStars();          void stepStars();           // twinkle
void initFunnyBubbles();   void stepFunnyBubbles();    // bouncy balls
void initBurst();          void stepBurst();           // pixel fireworks
void initFractals();      void stepFractals();       // smooth Mandelbrot fractal
