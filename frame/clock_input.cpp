// clock_input.cpp — see clock_input.h for the public API.
//
// --- WIRING (eurorack clock → Teensy 4.1) ---
//   Eurorack clock signals are typically 0V → +5V gates. Teensy 4.x pins are
//   3.3V max — feeding them +5V will damage the chip. Use a divider:
//
//     TIP (signal)  ─┬─ 10k ──● Teensy CLOCK_INPUT_PIN
//                    │        │
//                    │        ├─ 18k ── GND
//                    │        │
//                    │        └─ (optional) Schottky diode (BAT54/BAT85)
//                    │             anode → pin, cathode → 3V3 rail
//                    │             clamps over-voltage from ±12V sources
//     SLEEVE (gnd) ──┴────────── GND
//
//   With 10k+18k the high level on the pin is 5V × 18/(10+18) ≈ 3.21V — well
//   above the Teensy's HIGH threshold (~2.0V) and safely below 3.3V. The
//   optional clamping diode protects the pin if the source ever swings higher
//   (e.g. a +/-10V LFO miswired into the input).
//
// --- BPM MATH ---
//   Assumed PPQN = 1: every pulse = one beat. interval_ms = 60000 / BPM.
//   We keep a small ring of timestamps, derive intervals on demand, and take
//   the median so a single missed/extra pulse doesn't yank the readout.
//
// --- ISR SAFETY ---
//   The ISR only writes to volatile uint32_t / uint8_t fields — both 32-bit
//   atomic on ARM Cortex-M7. Reads in the foreground guard with
//   noInterrupts()/interrupts() when snapshotting multiple fields together.
#include "config.h"
#include "clock_input.h"

static volatile uint32_t lastPulseMs = 0;
static volatile uint32_t intervalMs = 0;
static volatile bool newInterval = false;

static float bpmFast = 0.0f;
static float candidateBpm = 0.0f;
static uint8_t candidateCount = 0;

static void onClockPulse() {
  uint32_t now = millis();

  if (lastPulseMs != 0) {
    uint32_t dt = now - lastPulseMs;

    if (dt < CLOCK_DEBOUNCE_MS) return;

    intervalMs = dt;
    newInterval = true;
  }

  lastPulseMs = now;
}

void initClockInput() {
  pinMode(CLOCK_INPUT_PIN, INPUT);

  lastPulseMs = 0;
  intervalMs = 0;
  newInterval = false;

  bpmFast = 0.0f;
  candidateBpm = 0.0f;
  candidateCount = 0;

  attachInterrupt(digitalPinToInterrupt(CLOCK_INPUT_PIN), onClockPulse, RISING);
}

uint32_t clockMsSinceLastPulse() {
  noInterrupts();
  uint32_t last = lastPulseMs;
  interrupts();

  if (last == 0) return UINT32_MAX;
  return millis() - last;
}

bool clockActive() {
  uint32_t since = clockMsSinceLastPulse();
  if (since == UINT32_MAX) return false;

  if (bpmFast < 1.0f) {
    return since < 700;
  }

  uint32_t beatMs = (uint32_t)(60000.0f / bpmFast);
  uint32_t timeout = beatMs * 2;

  if (timeout < 350) timeout = 350;
  if (timeout > CLOCK_TIMEOUT_MS) timeout = CLOCK_TIMEOUT_MS;

  bool active = since < timeout;

  if (!active) {
    // Fully reset detector after clock loss.
    bpmFast = 0.0f;
    candidateBpm = 0.0f;
    candidateCount = 0;
  }

  return active;
}

float clockBpm() {
  bool got = false;
  uint32_t dt = 0;

  noInterrupts();
  if (newInterval) {
    newInterval = false;
    got = true;
    dt = intervalMs;
  }
  interrupts();

  if (got && dt >= CLOCK_DEBOUNCE_MS && dt <= 3000) {
    float instant = 60000.0f / (float)dt;

    if (instant >= 20.0f && instant <= 400.0f) {
      if (bpmFast < 1.0f) {
        bpmFast = instant;
        candidateBpm = 0.0f;
        candidateCount = 0;
      } else {
        float ratio = instant / bpmFast;

        // Normal tempo movement: accept quickly.
        if (ratio > 0.70f && ratio < 1.45f) {
          bpmFast = bpmFast * 0.35f + instant * 0.65f;
          candidateBpm = 0.0f;
          candidateCount = 0;
        } else {
          // Far jump: maybe a real tempo change, maybe bad pulse.
          // Accept only if it repeats consistently.
          if (candidateBpm < 1.0f) {
            candidateBpm = instant;
            candidateCount = 1;
          } else {
            float cr = instant / candidateBpm;

            if (cr > 0.90f && cr < 1.10f) {
              candidateBpm = candidateBpm * 0.50f + instant * 0.50f;
              candidateCount++;

              if (candidateCount >= 3) {
                bpmFast = candidateBpm;
                candidateBpm = 0.0f;
                candidateCount = 0;
              }
            } else {
              candidateBpm = instant;
              candidateCount = 1;
            }
          }
        }
      }
    }
  }

  if (!clockActive()) return 0.0f;
  return bpmFast;
}

float clockPulseBrightness() {
  uint32_t since = clockMsSinceLastPulse();
  if (since == UINT32_MAX) return 0.0f;

  float bpm = clockBpm();
  if (bpm < 1.0f) bpm = 60.0f;

  uint32_t beatMs = (uint32_t)(60000.0f / bpm);
  if (beatMs < 80) beatMs = 80;

  if (since >= beatMs) return 0.15f;

  float t = 1.0f - (float)since / (float)beatMs;
  return 0.15f + 0.85f * t * t;
}