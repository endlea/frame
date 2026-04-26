// screensavers.cpp — unity-include wrapper.
//
// Why this file exists: Arduino IDE only auto-compiles .cpp/.h at the sketch
// root and recursively under src/. A /screensavers/ folder at the root would
// be ignored by the build. Putting everything in src/screensavers/ works but
// adds an extra layer of nesting.
//
// Trick: this single .cpp at the sketch root #includes every implementation
// file under /screensavers/. The compiler then sees one translation unit
// containing all the pattern code — exactly as if all those files had been
// listed directly.
//
// Consequences (handled in the files below):
//   * file-scope `static` symbols share the same TU, so duplicate names like
//     `static int randInt(...)` would collide. We define the shared helpers
//     once as `inline` in helpers.h and every pattern includes it.
//   * file-scope `static const`s with the same name (e.g. N_BUBBLES) also
//     collide — funny_bubbles.cpp's slot count was renamed to N_FUNNY for
//     this reason.
//
// This file should contain nothing but the includes below.
#include "screensavers/bubbles.cpp"
#include "screensavers/rain.cpp"
#include "screensavers/stars.cpp"
#include "screensavers/funny_bubbles.cpp"
#include "screensavers/burst.cpp"
#include "screensavers/fractals.cpp"
#include "screensavers/dispatcher.cpp"
