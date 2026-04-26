// clock_input.h — external eurorack clock (mono jack) tracker.
//
// Hardware: a 3.5mm mono jack (e.g. Thonkiconn). Tip → voltage divider → the
// pin defined in config.h::CLOCK_INPUT_PIN. Sleeve → Teensy GND. The Teensy
// 4.x is NOT 5V tolerant, so the divider is mandatory — see clock_input.cpp
// for the recommended resistor values.
//
// Software: a pin-change ISR captures the millis() of every rising edge,
// debounced. clockBpm() returns a median-smoothed estimate over the last few
// intervals. clockPulseBrightness() returns a 0..1 value that peaks on every
// pulse and decays over one beat — drive it into a colour lerp to get a
// "blinking LED" effect on the UI.
//
// Assumed PPQN = 1 (one pulse per beat). If you feed a 24-PPQN MIDI-style
// clock, multiply BPM accordingly later, or divide pulses upstream.
#pragma once
#include <Arduino.h>

// Set up the pin and attach the ISR. Call once from setup().
void initClockInput();

// True if at least one rising edge happened in the last CLOCK_TIMEOUT_MS ms.
bool clockActive();

// Median-smoothed BPM. Returns 0 if we don't have enough samples yet.
float clockBpm();

// Time in ms since the most recent pulse. UINT32_MAX if no pulses ever.
uint32_t clockMsSinceLastPulse();

// 0..1 brightness factor — 1.0 right after a pulse, decays (quadratically)
// to a small floor over one beat at the current BPM. Use this to lerp
// COLOR_DIM ↔ COLOR_ACCENT for the on-screen indicator.
float clockPulseBrightness();
