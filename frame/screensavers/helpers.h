// helpers.h — random helpers shared by every pattern.
//
// Why this exists: all pattern .cpp files end up in the SAME translation unit
// (they're #include'd from the root-level screensavers.cpp wrapper, see
// screensaver.h for the why). Each file used to define its own
// `static int randInt(...)` and `static float randFloat(...)` — once they all
// share a TU those duplicates become a redefinition error.
//
// Solution: define them once here as `inline`. Every .cpp includes this
// header, the linker (well, the compiler — there's only one TU) sees one
// definition. No collisions.
#pragma once
#include <Arduino.h>

inline int randInt(int lo, int hi) {
  return lo + (int)random(hi - lo);
}

inline float randFloat(float lo, float hi) {
  return lo + (hi - lo) * (float)random(10000) / 10000.0f;
}
