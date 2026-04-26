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

// ---- Tunables ----
// Length of the timestamp ring. N+1 timestamps → N intervals between them.
// We only use the most recent interval for the BPM readout (max-snappy: tempo
// changes show up on the very next pulse). The ring is kept at 2 just so we
// always have one prev timestamp to subtract from. Pamela-class digital
// clocks have ≤ ±1 ms jitter, way below the BPM display granularity, so no
// smoothing is needed for a clean readout.
static const int N_INTERVALS = 2;

// ---- ISR-shared state ----
// Ring of millis() at each accepted rising edge, oldest → newest.
static volatile uint32_t pulseTimes[N_INTERVALS + 1];
// Index of the newest entry in pulseTimes.
static volatile uint8_t  pulseHead  = 0;
// Total accepted pulses, capped at N_INTERVALS+1 (we only need to know
// whether the ring is fully populated).
static volatile uint8_t  pulseCount = 0;

// ---- ISR ----
static void onClockPulse() {
  uint32_t now  = millis();
  uint32_t prev = pulseTimes[pulseHead];

  // Debounce: ignore pulses that come too soon after the last one.
  if (pulseCount > 0 && (now - prev) < CLOCK_DEBOUNCE_MS) return;

  uint8_t next = (uint8_t)((pulseHead + 1) % (N_INTERVALS + 1));
  pulseTimes[next] = now;
  pulseHead = next;
  if (pulseCount < (uint8_t)(N_INTERVALS + 1)) pulseCount++;
}

void initClockInput() {
  // External divider already biases the pin — no internal pull-up needed.
  pinMode(CLOCK_INPUT_PIN, INPUT);

  pulseHead  = 0;
  pulseCount = 0;
  for (int i = 0; i <= N_INTERVALS; i++) pulseTimes[i] = 0;

  attachInterrupt(digitalPinToInterrupt(CLOCK_INPUT_PIN),
                  onClockPulse, RISING);
}

uint32_t clockMsSinceLastPulse() {
  noInterrupts();
  uint8_t  cnt  = pulseCount;
  uint32_t last = pulseTimes[pulseHead];
  interrupts();
  if (cnt == 0) return UINT32_MAX;
  return millis() - last;
}

bool clockActive() {
  uint32_t since = clockMsSinceLastPulse();
  if (since == UINT32_MAX) return false;

  // Adaptive timeout: declare the clock dead ~1.5 beats after the last pulse
  // at the currently detected tempo. This way the indicator disappears
  // promptly when the clock stops, regardless of BPM, without flickering
  // between pulses at fast tempos.
  //   * floor 400 ms — keeps tempos above ~225 BPM from killing the indicator
  //     in the gap between two consecutive pulses
  //   * ceiling CLOCK_TIMEOUT_MS — also used as fallback when we don't have
  //     a tempo yet (only one pulse seen so far)
  float bpm = clockBpm();
  uint32_t timeout;
  if (bpm > 1.0f) {
    timeout = (uint32_t)(60000.0f / bpm) * 3 / 2;
    if (timeout < 400) timeout = 400;
    if (timeout > CLOCK_TIMEOUT_MS) timeout = CLOCK_TIMEOUT_MS;
  } else {
    timeout = CLOCK_TIMEOUT_MS;
  }
  return since < timeout;
}

float clockBpm() {
  // Atomically grab the two most recent timestamps. We only need the latest
  // interval — that's what makes the readout react on the very next pulse.
  noInterrupts();
  uint8_t  cnt  = pulseCount;
  uint8_t  head = pulseHead;
  uint32_t last = pulseTimes[head];
  uint32_t prev = pulseTimes[(head + (N_INTERVALS + 1) - 1) % (N_INTERVALS + 1)];
  interrupts();

  if (cnt < 2) return 0.0f;
  uint32_t dt = last - prev;
  if (dt == 0) return 0.0f;
  return 60000.0f / (float)dt;
}

float clockPulseBrightness() {
  uint32_t since = clockMsSinceLastPulse();
  if (since == UINT32_MAX) return 0.0f;

  // Decay window = one beat at the current tempo. Fall back to 60 BPM if we
  // don't have enough samples yet — that gives a sensible visible blink even
  // on the very first pulse.
  float bpm = clockBpm();
  if (bpm < 1.0f) bpm = 60.0f;
  uint32_t beatMs = (uint32_t)(60000.0f / bpm);
  if (beatMs < 50) beatMs = 50;

  if (since >= beatMs) return 0.15f;     // dim floor between beats

  // Quadratic ease-out so the bright peak is snappy, not sluggish.
  float t = 1.0f - (float)since / (float)beatMs;
  return 0.15f + 0.85f * t * t;
}
