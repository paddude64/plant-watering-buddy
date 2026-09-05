/*
  Plant Watering Buddy
  ====================
  M5Stack Atom Lite + M5Stack Watering Unit (U101).

  Reads soil moisture and waters the plant in small doses when it gets dry.
  Runs standalone: no WiFi, no server, no account. Power it from a USB
  charger and leave it alone.

  ---------------------------------------------------------------------
  THE ONE IDEA THAT MATTERS: PULSE, THEN WAIT
  ---------------------------------------------------------------------
  The obvious design is "run the pump until the sensor reads wet". That
  design floods plants.

  The reason is soak lag. The probe sits several centimetres down, and
  water poured on the surface takes minutes to reach it. So the sensor
  keeps reporting "dry" long after plenty of water has gone in, and the
  pump keeps running. The sensor is not lying — it is reporting the past.

  So instead: deliver a small measured dose, then STOP AND IGNORE THE
  SENSOR while the water works its way down. Only then take a fresh
  reading and decide whether more is needed. The sensor is never trusted
  while water is in transit.

  Everything else in this file is about refusing to water when something
  is wrong, because the failure that matters is not "slightly too dry" —
  it is "the pump ran all night". Each guard is commented where it lives;
  the README has the overview.

  ---------------------------------------------------------------------
  SETTING IT UP
  ---------------------------------------------------------------------
  Connect over USB serial at 115200 and type `help`. In short:

    1. `cal dry`  with the probe clean and dry in open air
    2. `cal wet`  with the probe in a glass of water
    3. `set pulse <seconds>` from the flow rate measured by sketch 02 —
       see docs/hardware.md
    4. `save`

  Calibration lives in flash and survives reflashing, so this is a
  once-per-device job, not a once-per-build job.

  No libraries are used beyond the ESP32 core itself. See
  docs/led-and-buttons.md for what the light means.
*/

#include <Preferences.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

const char *FIRMWARE_VERSION = "0.2.0-dev";

// ===========================================================================
// SECTION 1 — Hardware
// ===========================================================================

const int PUMP_PIN = 26;      // Grove yellow. HIGH runs the pump.
const int MOISTURE_PIN = 32;  // Grove white. Analog in.
const int LED_PIN = 27;       // On-board SK6812.
const int BTN_PIN = 39;       // On-board button. LOW when pressed.

// GPIO 39 is input-only and has no internal pull-up, so INPUT_PULLUP would
// compile and silently not work. The Atom Lite has an external pull-up.
//
// GPIO 32 is on ADC1. That matters: ADC2 stops working when WiFi is on, so
// if this project ever grows a network, the sensor keeps reading.

// ===========================================================================
// SECTION 2 — Safety limits
//
// These are deliberately NOT user-settable and NOT stored in flash. They are
// the backstop behind every configurable value below, so that a typo in a
// serial command cannot talk the device into running the pump for an hour.
// ===========================================================================

const unsigned long PUMP_HARD_MAX_MS = 20UL * 1000UL;   // absolute ceiling on
                                                        // one pump activation
const unsigned long MANUAL_MAX_MS = 15UL * 1000UL;      // button-held override
const int PULSE_SECONDS_MAX = 15;                       // ceiling on `set pulse`
const int MAX_PULSES_PER_DAY_MAX = 20;

// A reading outside this band is a wiring fault, not soil. Narrow these once
// you have seen what your own sensor actually does in sketch 03.
const int SENSOR_RAW_MIN = 100;
const int SENSOR_RAW_MAX = 4000;

// Calibration is rejected if dry and wet are closer together than this — it
// means one of the two readings was taken wrongly.
const int MIN_CAL_SPREAD = 200;

// How many consecutive doses may produce no moisture rise before we conclude
// the water is not arriving.
const int MAX_UNRESPONSIVE_PULSES = 3;
const int MIN_RESPONSE_PCT = 5;

// ===========================================================================
// SECTION 3 — Settings (stored in flash, changeable over serial)
// ===========================================================================

struct Settings {
  int dryRaw;            // reading with the probe dry in air
  int wetRaw;            // reading with the probe in water
  int pulseSeconds;      // how long one dose runs the pump
  int soakMinutes;       // how long to ignore the sensor after a dose
  int maxPulsesPerDay;
  int waterBelowPct;     // start watering below this moisture percentage
  int stopAbovePct;      // consider the job done above this percentage
  bool calibrated;
};

Settings cfg = {
  .dryRaw = 0,
  .wetRaw = 0,
  .pulseSeconds = 3,
  .soakMinutes = 20,
  .maxPulsesPerDay = 8,
  .waterBelowPct = 30,
  .stopAbovePct = 55,
  .calibrated = false,
};

Preferences prefs;

// ===========================================================================
// SECTION 4 — State
// ===========================================================================

enum State {
  ST_NEEDS_CALIBRATION,
  ST_IDLE,      // soil is fine, just watching
  ST_WATERING,  // a dose is being delivered right now
  ST_SOAKING,   // dose delivered, waiting for it to reach the probe
  ST_MANUAL,    // button held, pump forced on
  ST_FAULT,     // sensor reading is implausible
  ST_LOCKOUT,   // safety stop, needs a person
};

enum LockReason {
  LOCK_NONE,
  LOCK_NO_RESPONSE,   // watered repeatedly, soil never got wetter
  LOCK_DAILY_CAP,     // used up the day's allowance
  LOCK_HARD_TIMEOUT,  // pump exceeded its absolute maximum runtime
};

State state = ST_NEEDS_CALIBRATION;
LockReason lockReason = LOCK_NONE;

// Whether the hardware watchdog actually armed. Checked rather than assumed:
// a watchdog you believe in but do not have is worse than none.
bool watchdogArmed = false;

// Brownouts seen since the counter was last cleared, kept in flash.
//
// This is a diagnosis aid, not a safeguard — setup() already switches the
// pump off after any reset, whatever caused it. It exists because a
// brownout imitates a plumbing fault: the pump cuts out mid-dose, the soil
// never gets wetter, and after three such doses the device locks out
// reporting that water is not reaching the plant. That sends someone to
// check the jar and the tubing when the real problem is the power supply.
// A count here lets `status` tell them apart.
int brownoutCount = 0;

bool pumpRunning = false;
bool manualLimitReported = false;  // so the timeout is announced once, not
                                   // once per pass of loop()
unsigned long pumpStartedAt = 0;
unsigned long stateEnteredAt = 0;

// Watering-cycle bookkeeping
int pctAtCycleStart = 0;       // moisture when the current dose sequence began
int unresponsivePulses = 0;
int pulsesToday = 0;
unsigned long dayWindowStartedAt = 0;

// ===========================================================================
// SECTION 5 — Moisture reading
//
// A rolling average, because a single ADC sample is noisy enough to flip a
// threshold on its own. We want the pump to respond to the soil drying out
// over hours, not to electrical noise over milliseconds.
// ===========================================================================

const int SAMPLE_COUNT = 8;
const unsigned long SAMPLE_INTERVAL_MS = 2000;

int samples[SAMPLE_COUNT];
int sampleCount = 0;
int sampleIndex = 0;
unsigned long lastSampleAt = 0;

void addSample(int raw) {
  samples[sampleIndex] = raw;
  sampleIndex = (sampleIndex + 1) % SAMPLE_COUNT;
  if (sampleCount < SAMPLE_COUNT) sampleCount++;
}

int smoothedRaw() {
  if (sampleCount == 0) return analogRead(MOISTURE_PIN);
  long sum = 0;
  for (int i = 0; i < sampleCount; i++) sum += samples[i];
  return (int)(sum / sampleCount);
}

bool readingPlausible(int raw) {
  return raw >= SENSOR_RAW_MIN && raw <= SENSOR_RAW_MAX;
}

// Convert a raw reading to 0-100%, where 100% is "as wet as the calibration
// glass of water". The subtraction carries its own sign, so this works
// whether your sensor reads higher or lower when wet — which is why there is
// no "WETTER_WHEN_LOWER" setting to get wrong.
int moisturePercent(int raw) {
  if (!cfg.calibrated) return -1;
  long span = (long)cfg.wetRaw - (long)cfg.dryRaw;
  if (span == 0) return -1;
  long pct = ((long)raw - (long)cfg.dryRaw) * 100 / span;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return (int)pct;
}

// ===========================================================================
// SECTION 6 — LED
//
// One LED has to convey the whole state of the machine, so: colour says what
// state we are in, and movement says whether we are alive. A device that is
// merely idle and a device that has crashed must never look the same.
// ===========================================================================

const uint8_t LED_LEVEL = 40;  // out of 255; it is a very bright LED

void setLed(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(LED_PIN, r, g, b); }

void updateLed() {
  unsigned long t = millis();

  // A slow triangle wave, 0..LED_LEVEL over ~4 seconds, for "breathing".
  unsigned long phase = t % 4000;
  uint8_t breath = (phase < 2000) ? (phase * LED_LEVEL / 2000)
                                  : ((4000 - phase) * LED_LEVEL / 2000);
  // Two clearly different rates: a fault you can fix with a cable is not the
  // same as a lockout that wants you to go and look at the plant, and they
  // must not be tellable apart only by hue.
  bool blinkFast = (t % 500) < 250;
  bool blinkSlow = (t % 1600) < 800;

  switch (state) {
    case ST_NEEDS_CALIBRATION:
      setLed(LED_LEVEL, LED_LEVEL, 0);            // yellow, steady
      break;
    case ST_IDLE:
      setLed(0, 0, breath);                       // blue, breathing
      break;
    case ST_WATERING:
      setLed(0, LED_LEVEL, 0);                    // green, steady
      break;
    case ST_SOAKING:
      setLed(0, breath, breath);                  // cyan, breathing
      break;
    case ST_MANUAL:
      setLed(LED_LEVEL, LED_LEVEL, LED_LEVEL);    // white, steady
      break;
    case ST_FAULT:
      setLed(blinkFast ? LED_LEVEL : 0, 0, blinkFast ? LED_LEVEL : 0);  // magenta
      break;
    case ST_LOCKOUT:
      setLed(blinkSlow ? LED_LEVEL : 0, 0, 0);    // red, slow blink
      break;
  }
}

// ===========================================================================
// SECTION 7 — Pump
//
// Every path that energises the pump goes through pumpOn(). There is exactly
// one place in this file that writes HIGH to the pump pin, and one hard
// timeout checked on every pass of the main loop that can write it back LOW.
// ===========================================================================

void pumpOn() {
  if (pumpRunning) return;
  digitalWrite(PUMP_PIN, HIGH);
  pumpRunning = true;
  pumpStartedAt = millis();
}

void pumpOff() {
  digitalWrite(PUMP_PIN, LOW);  // unconditional: cheap, and safe to repeat
  pumpRunning = false;
}

void enterState(State s) {
  state = s;
  stateEnteredAt = millis();
}

void lockout(LockReason reason) {
  pumpOff();
  lockReason = reason;
  enterState(ST_LOCKOUT);
  Serial.print("LOCKOUT: ");
  Serial.println(lockReasonText());
}

const char *lockReasonText() {
  switch (lockReason) {
    case LOCK_NO_RESPONSE:
      return "watered repeatedly but the soil never got wetter — check the "
             "water jar, the tube, and that the probe is still in the soil "
             "(and check `status` for brownouts: a supply that cannot start "
             "the pump looks exactly like this)";
    case LOCK_DAILY_CAP:
      return "used up the maximum doses for one day";
    case LOCK_HARD_TIMEOUT:
      return "pump exceeded its absolute maximum runtime";
    default:
      return "none";
  }
}

const char *stateText() {
  switch (state) {
    case ST_NEEDS_CALIBRATION: return "NEEDS CALIBRATION";
    case ST_IDLE:              return "idle";
    case ST_WATERING:          return "watering";
    case ST_SOAKING:           return "soaking";
    case ST_MANUAL:            return "manual override";
    case ST_FAULT:             return "SENSOR FAULT";
    case ST_LOCKOUT:           return "LOCKED OUT";
  }
  return "?";
}

// ===========================================================================
// SECTION 8 — Button
//
// Click, double-click and hold, done without blocking. Keeping this in one
// place with explicit state makes it possible to reason about, and means the
// main loop never waits for a finger.
// ===========================================================================

const unsigned long DEBOUNCE_MS = 25;
const unsigned long HOLD_MS = 800;
const unsigned long DOUBLE_GAP_MS = 400;

bool btnStable = false;        // debounced: true means pressed
bool btnLastRaw = false;
unsigned long btnChangedAt = 0;
unsigned long btnPressedAt = 0;
unsigned long btnLastReleaseAt = 0;
bool btnHoldReported = false;
bool clickPending = false;
unsigned long clickPendingAt = 0;

bool evClick = false;
bool evDoubleClick = false;
bool evHolding = false;

void updateButton() {
  unsigned long now = millis();
  bool raw = (digitalRead(BTN_PIN) == LOW);

  evClick = false;
  evDoubleClick = false;

  if (raw != btnLastRaw) {
    btnLastRaw = raw;
    btnChangedAt = now;
  }

  // Accept the new level only once it has been steady long enough.
  if (now - btnChangedAt > DEBOUNCE_MS && raw != btnStable) {
    btnStable = raw;
    if (btnStable) {
      btnPressedAt = now;
      btnHoldReported = false;
    } else {
      if (!btnHoldReported) {
        // A short press. Was it the second of a pair?
        if (now - btnLastReleaseAt < DOUBLE_GAP_MS) {
          evDoubleClick = true;
          clickPending = false;  // it was half of a double, not a click
        } else {
          // Don't report it yet: a second press might still be coming, and
          // firing a click now would mean every double-click also ran the
          // single-click action first.
          clickPending = true;
          clickPendingAt = now;
        }
        btnLastReleaseAt = now;
      }
    }
  }

  // Long enough has passed that no second press is coming: it was a single.
  if (clickPending && now - clickPendingAt > DOUBLE_GAP_MS) {
    clickPending = false;
    evClick = true;
  }

  evHolding = btnStable && (now - btnPressedAt > HOLD_MS);
  if (evHolding) btnHoldReported = true;  // suppress the click on release
}

// ===========================================================================
// SECTION 9 — Settings persistence
// ===========================================================================

void loadSettings() {
  prefs.begin("pwb", false);
  cfg.dryRaw = prefs.getInt("dryRaw", cfg.dryRaw);
  cfg.wetRaw = prefs.getInt("wetRaw", cfg.wetRaw);
  cfg.pulseSeconds = prefs.getInt("pulseSec", cfg.pulseSeconds);
  cfg.soakMinutes = prefs.getInt("soakMin", cfg.soakMinutes);
  cfg.maxPulsesPerDay = prefs.getInt("maxDay", cfg.maxPulsesPerDay);
  cfg.waterBelowPct = prefs.getInt("waterPct", cfg.waterBelowPct);
  cfg.stopAbovePct = prefs.getInt("stopPct", cfg.stopAbovePct);
  // Falls back to the compile-time default like every other field above,
  // rather than a hardcoded false. That makes it possible to ship a sketch
  // with calibration baked in — edit dryRaw/wetRaw and set this true — for
  // anyone who would rather not do it over serial. See docs/bring-up.md
  // Step 5 for when that is and is not a good idea.
  cfg.calibrated = prefs.getBool("cal", cfg.calibrated);

  // The day's dose count survives a reboot on purpose. Without a real-time
  // clock this device cannot tell whether it was off for a minute or a
  // fortnight, so it assumes the worst and keeps counting. That can only ever
  // make it water less than it should, which is the safe direction to be
  // wrong in. `clear` resets it.
  pulsesToday = prefs.getInt("pulsesDay", 0);

  brownoutCount = prefs.getInt("brownouts", 0);
  if (esp_reset_reason() == ESP_RST_BROWNOUT) {
    brownoutCount++;
    prefs.putInt("brownouts", brownoutCount);
  }
}

void saveSettings() {
  prefs.putInt("dryRaw", cfg.dryRaw);
  prefs.putInt("wetRaw", cfg.wetRaw);
  prefs.putInt("pulseSec", cfg.pulseSeconds);
  prefs.putInt("soakMin", cfg.soakMinutes);
  prefs.putInt("maxDay", cfg.maxPulsesPerDay);
  prefs.putInt("waterPct", cfg.waterBelowPct);
  prefs.putInt("stopPct", cfg.stopAbovePct);
  prefs.putBool("cal", cfg.calibrated);
  Serial.println("Saved to flash.");
}

void savePulseCount() { prefs.putInt("pulsesDay", pulsesToday); }

// ===========================================================================
// SECTION 10 — Calibration
// ===========================================================================

void calibrateDry() {
  int raw = smoothedRaw();
  Serial.printf("Dry reference set to %d\n", raw);
  cfg.dryRaw = raw;
  checkCalibrationValid();
}

void calibrateWet() {
  int raw = smoothedRaw();
  Serial.printf("Wet reference set to %d\n", raw);
  cfg.wetRaw = raw;
  checkCalibrationValid();
}

void checkCalibrationValid() {
  if (cfg.dryRaw == 0 || cfg.wetRaw == 0) {
    cfg.calibrated = false;
    Serial.println("Need both `cal dry` and `cal wet`.");
    return;
  }
  int spread = abs(cfg.wetRaw - cfg.dryRaw);
  if (spread < MIN_CAL_SPREAD) {
    cfg.calibrated = false;
    Serial.printf(
        "Rejected: dry (%d) and wet (%d) are only %d apart, expected at least "
        "%d. One of those readings was taken wrongly — the probe needs to be "
        "properly dry for one and properly submerged for the other.\n",
        cfg.dryRaw, cfg.wetRaw, spread, MIN_CAL_SPREAD);
    return;
  }
  cfg.calibrated = true;
  Serial.printf("Calibrated. dry=%d wet=%d (spread %d). `save` to keep it.\n",
                cfg.dryRaw, cfg.wetRaw, spread);
  if (state == ST_NEEDS_CALIBRATION) enterState(ST_IDLE);
}

// ===========================================================================
// SECTION 11 — The control loop
// ===========================================================================

// Roll the 24-hour dose window over if a day of uptime has passed.
void updateDayWindow() {
  if (millis() - dayWindowStartedAt > 24UL * 60UL * 60UL * 1000UL) {
    dayWindowStartedAt = millis();
    if (pulsesToday != 0) {
      pulsesToday = 0;
      savePulseCount();
      Serial.println("New day: dose allowance reset.");
    }
    // Running out of doses for the day is not a fault, it is a budget. It
    // should expire on its own rather than needing someone to come and press
    // a button — unlike LOCK_NO_RESPONSE, which means something is actually
    // broken and a person genuinely does need to look at it.
    if (state == ST_LOCKOUT && lockReason == LOCK_DAILY_CAP) {
      lockReason = LOCK_NONE;
      enterState(ST_IDLE);
      Serial.println("Daily dose lockout expired; watching the soil again.");
    }
  }
}

// Begin one dose. Everything that could refuse has already been checked by
// the caller except the daily cap, which lives here so that every route to
// the pump — automatic or the `water` command — is subject to it.
bool startPulse() {
  if (pulsesToday >= cfg.maxPulsesPerDay) {
    lockout(LOCK_DAILY_CAP);
    return false;
  }
  pulsesToday++;
  savePulseCount();
  pumpOn();
  enterState(ST_WATERING);
  Serial.printf("Dose %d of %d for today: pump on for %ds\n", pulsesToday,
                cfg.maxPulsesPerDay, cfg.pulseSeconds);
  return true;
}

void runControl() {
  unsigned long now = millis();
  int raw = smoothedRaw();
  int pct = moisturePercent(raw);

  // ---- Checks that apply in every state, before any state-specific logic --

  // The absolute backstop. If anything at all has gone wrong in the logic
  // below, this is what stops the pump.
  if (pumpRunning && now - pumpStartedAt > PUMP_HARD_MAX_MS) {
    lockout(LOCK_HARD_TIMEOUT);
    return;
  }

  // A wildly out-of-range reading means the probe is unplugged or broken.
  // Treat it as a fault, not as very dry soil — reading it as dry soil would
  // start the pump on a device that cannot see anything.
  if (sampleCount >= SAMPLE_COUNT && !readingPlausible(raw)) {
    if (state != ST_FAULT && state != ST_LOCKOUT) {
      pumpOff();
      enterState(ST_FAULT);
      Serial.printf("Sensor reading %d is outside the plausible range %d-%d. "
                    "Is the Grove cable connected?\n",
                    raw, SENSOR_RAW_MIN, SENSOR_RAW_MAX);
    }
    return;
  }
  if (state == ST_FAULT) {
    Serial.println("Sensor reading is sensible again.");
    enterState(cfg.calibrated ? ST_IDLE : ST_NEEDS_CALIBRATION);
  }

  updateDayWindow();

  // ---- Manual override: holding the button runs the pump ------------------
  // Allowed even when locked out. If someone is standing there holding the
  // button down, they can see what is happening better than we can.
  if (evHolding && state != ST_NEEDS_CALIBRATION) {
    if (state != ST_MANUAL) {
      Serial.println("Manual override: pump on while held.");
      enterState(ST_MANUAL);
      manualLimitReported = false;
      pumpOn();
    }
    // Note pumpStartedAt is deliberately not reset when the override takes
    // over a dose already in progress: the limit then covers the total run,
    // which errs towards less water.
    if (now - pumpStartedAt > MANUAL_MAX_MS) {
      if (!manualLimitReported) {
        Serial.println("Manual override hit its time limit.");
        manualLimitReported = true;
      }
      pumpOff();
    }
    return;
  }
  if (state == ST_MANUAL) {
    pumpOff();
    manualLimitReported = false;
    Serial.println("Manual override released.");
    enterState(lockReason == LOCK_NONE ? ST_IDLE : ST_LOCKOUT);
    return;
  }

  // ---- Per-state logic ----------------------------------------------------
  switch (state) {
    case ST_NEEDS_CALIBRATION:
    case ST_LOCKOUT:
      pumpOff();  // belt and braces; nothing here should have turned it on
      break;

    case ST_IDLE:
      // Resting states all assert the pump is off, so that no transition into
      // one can leave it running.
      if (pumpRunning) pumpOff();
      if (pct >= 0 && pct < cfg.waterBelowPct) {
        Serial.printf("Soil at %d%%, below the %d%% mark. Starting to water.\n",
                      pct, cfg.waterBelowPct);
        pctAtCycleStart = pct;
        unresponsivePulses = 0;
        startPulse();
      }
      break;

    case ST_WATERING:
      if (now - stateEnteredAt >= (unsigned long)cfg.pulseSeconds * 1000UL) {
        pumpOff();
        enterState(ST_SOAKING);
        Serial.printf("Dose delivered. Soaking for %d minutes before looking "
                      "at the sensor again.\n",
                      cfg.soakMinutes);
      }
      break;

    case ST_SOAKING:
      // The sensor is deliberately ignored for this whole period. This is the
      // heart of the design: water is in transit and the reading is stale.
      if (now - stateEnteredAt <
          (unsigned long)cfg.soakMinutes * 60UL * 1000UL) {
        break;
      }

      if (pct >= cfg.stopAbovePct) {
        Serial.printf("Soil now at %d%%. Done watering.\n", pct);
        enterState(ST_IDLE);
        break;
      }

      // Still dry. Did the last dose move the needle at all?
      if (pct - pctAtCycleStart < MIN_RESPONSE_PCT) {
        unresponsivePulses++;
        Serial.printf(
            "Soil at %d%%, started at %d%% — barely moved (%d in a row).\n",
            pct, pctAtCycleStart, unresponsivePulses);
        if (unresponsivePulses >= MAX_UNRESPONSIVE_PULSES) {
          lockout(LOCK_NO_RESPONSE);
          break;
        }
      } else {
        // It is responding, just not there yet. Reset the patience counter and
        // measure the next dose from here.
        unresponsivePulses = 0;
        pctAtCycleStart = pct;
      }
      startPulse();
      break;

    default:
      break;
  }
}

// ===========================================================================
// SECTION 12 — Serial console
//
// This is the main way to work on the device: calibrate it, watch what it
// thinks, and force it to do things. It is also how to debug it from another
// country — ask for the output of `status` and it is all there.
// ===========================================================================

String inputLine;

void printStatus() {
  int raw = smoothedRaw();
  int pct = moisturePercent(raw);
  Serial.println();
  Serial.printf("Plant Watering Buddy %s\n", FIRMWARE_VERSION);
  Serial.printf("  state          : %s\n", stateText());
  if (state == ST_LOCKOUT) Serial.printf("  locked because : %s\n", lockReasonText());
  Serial.printf("  raw reading    : %d %s\n", raw,
                readingPlausible(raw) ? "" : "(IMPLAUSIBLE)");
  if (pct >= 0) Serial.printf("  moisture       : %d%%\n", pct);
  Serial.printf("  pump           : %s\n", pumpRunning ? "RUNNING" : "off");
  Serial.printf("  doses today    : %d of %d\n", pulsesToday, cfg.maxPulsesPerDay);
  Serial.printf("  uptime         : %lu min\n", millis() / 60000UL);
  Serial.printf("  watchdog       : %s\n", watchdogArmed ? "armed" : "NOT ARMED");
  Serial.printf("  brownouts      : %d%s\n", brownoutCount,
                brownoutCount ? "   <-- power supply, not plumbing" : "");
  Serial.println("  settings:");
  Serial.printf("    calibration  : %s (dry %d, wet %d)\n",
                cfg.calibrated ? "done" : "NOT DONE", cfg.dryRaw, cfg.wetRaw);
  Serial.printf("    dose         : %d s of pumping\n", cfg.pulseSeconds);
  Serial.printf("    soak         : %d min\n", cfg.soakMinutes);
  Serial.printf("    water below  : %d%%\n", cfg.waterBelowPct);
  Serial.printf("    stop above   : %d%%\n", cfg.stopAbovePct);
  Serial.printf("    max per day  : %d\n", cfg.maxPulsesPerDay);
  Serial.println();
}

void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  status              everything this device currently thinks");
  Serial.println("  read                one moisture reading");
  Serial.println("  cal dry             set the dry reference (probe dry, in air)");
  Serial.println("  cal wet             set the wet reference (probe in water)");
  Serial.println("  cal clear           forget calibration");
  Serial.println("  set pulse <sec>     how long one dose runs the pump");
  Serial.println("  set soak <min>      how long to ignore the sensor after a dose");
  Serial.println("  set below <pct>     start watering below this moisture");
  Serial.println("  set above <pct>     stop watering above this moisture");
  Serial.println("  set maxday <n>      doses allowed per day");
  Serial.println("  water               force one dose now");
  Serial.println("  stop                pump off (does NOT clear a lockout)");
  Serial.println("  clear               clear a lockout, dose count and brownouts");
  Serial.println("  save                write settings to flash");
  Serial.println();
}

void setSetting(const String &name, int value) {
  if (name == "pulse") {
    if (value < 1 || value > PULSE_SECONDS_MAX) {
      Serial.printf("Dose must be 1-%d seconds.\n", PULSE_SECONDS_MAX);
      return;
    }
    cfg.pulseSeconds = value;
  } else if (name == "soak") {
    if (value < 1 || value > 240) {
      Serial.println("Soak must be 1-240 minutes.");
      return;
    }
    cfg.soakMinutes = value;
  } else if (name == "below") {
    if (value < 1 || value >= cfg.stopAbovePct) {
      Serial.printf("Must be 1-%d, below the stop threshold.\n",
                    cfg.stopAbovePct - 1);
      return;
    }
    cfg.waterBelowPct = value;
  } else if (name == "above") {
    if (value <= cfg.waterBelowPct || value > 100) {
      Serial.printf("Must be %d-100, above the start threshold.\n",
                    cfg.waterBelowPct + 1);
      return;
    }
    cfg.stopAbovePct = value;
  } else if (name == "maxday") {
    if (value < 1 || value > MAX_PULSES_PER_DAY_MAX) {
      Serial.printf("Must be 1-%d.\n", MAX_PULSES_PER_DAY_MAX);
      return;
    }
    cfg.maxPulsesPerDay = value;
  } else {
    Serial.println("Unknown setting. Try `help`.");
    return;
  }
  Serial.printf("%s = %d (not saved yet — use `save`)\n", name.c_str(), value);
}

void clearLockout() {
  brownoutCount = 0;
  prefs.putInt("brownouts", 0);
  // Stop the pump first. Without this, clearing during a dose lands in
  // ST_IDLE with the pump still energised, and ST_IDLE is the one state that
  // does not defensively switch it off — so it would run to the hard ceiling.
  pumpOff();
  lockReason = LOCK_NONE;
  unresponsivePulses = 0;
  pulsesToday = 0;
  savePulseCount();
  dayWindowStartedAt = millis();
  enterState(cfg.calibrated ? ST_IDLE : ST_NEEDS_CALIBRATION);
  Serial.println("Cleared. Dose count reset to zero.");
}

void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) return;

  String verb = line;
  String rest = "";
  int sp = line.indexOf(' ');
  if (sp > 0) {
    verb = line.substring(0, sp);
    rest = line.substring(sp + 1);
    rest.trim();
  }
  verb.toLowerCase();
  rest.toLowerCase();

  if (verb == "help" || verb == "?") {
    printHelp();
  } else if (verb == "status") {
    printStatus();
  } else if (verb == "read") {
    int raw = smoothedRaw();
    int pct = moisturePercent(raw);
    Serial.printf("raw %d", raw);
    if (pct >= 0) Serial.printf("  =  %d%%", pct);
    if (!readingPlausible(raw)) Serial.print("  (IMPLAUSIBLE)");
    Serial.println();
  } else if (verb == "cal") {
    if (rest == "dry") calibrateDry();
    else if (rest == "wet") calibrateWet();
    else if (rest == "clear") {
      cfg.dryRaw = cfg.wetRaw = 0;
      cfg.calibrated = false;
      enterState(ST_NEEDS_CALIBRATION);
      Serial.println("Calibration cleared.");
    } else Serial.println("Use: cal dry | cal wet | cal clear");
  } else if (verb == "set") {
    int sp2 = rest.indexOf(' ');
    if (sp2 <= 0) {
      Serial.println("Use: set <name> <value>");
    } else {
      setSetting(rest.substring(0, sp2), rest.substring(sp2 + 1).toInt());
    }
  } else if (verb == "water") {
    if (!cfg.calibrated) {
      Serial.println("Calibrate first — otherwise the dose is a guess.");
    } else if (state == ST_LOCKOUT) {
      Serial.println("Locked out. `clear` first, once you have fixed the cause.");
    } else if (pumpRunning) {
      Serial.println("Already watering — wait for this dose to finish.");
    } else {
      pctAtCycleStart = moisturePercent(smoothedRaw());
      unresponsivePulses = 0;
      startPulse();
    }
  } else if (verb == "stop") {
    pumpOff();
    // `stop` reads as a safety action, so it must not quietly resume
    // automatic watering. A lockout is cleared only by `clear`, deliberately,
    // once a person has dealt with whatever caused it.
    if (state == ST_LOCKOUT) {
      Serial.println("Pump off. Still locked out — use `clear` once you have "
                     "fixed the cause.");
    } else {
      enterState(cfg.calibrated ? ST_IDLE : ST_NEEDS_CALIBRATION);
      Serial.println("Stopped.");
    }
  } else if (verb == "clear") {
    clearLockout();
  } else if (verb == "save") {
    saveSettings();
  } else {
    Serial.printf("Unknown command '%s'. Try `help`.\n", verb.c_str());
  }
}

void pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (inputLine.length() > 0) {
        handleCommand(inputLine);
        inputLine = "";
      }
    } else if (inputLine.length() < 80) {
      inputLine += c;
    }
  }
}

// ===========================================================================
// SECTION 13 — setup / loop
// ===========================================================================

void setup() {
  // Before anything else at all: make sure the pump is off. If the board has
  // just reset while the pump was running, this is the line that stops it.
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);

  pinMode(MOISTURE_PIN, INPUT);
  pinMode(BTN_PIN, INPUT);  // no pull-up: GPIO39 has none, the board provides it

  Serial.begin(115200);
  delay(500);

  loadSettings();
  dayWindowStartedAt = millis();
  enterState(cfg.calibrated ? ST_IDLE : ST_NEEDS_CALIBRATION);

  // Prime the rolling average so the first decision is not made on one sample.
  for (int i = 0; i < SAMPLE_COUNT; i++) addSample(analogRead(MOISTURE_PIN));

  // If the main loop ever stops running — a hang, a deadlock, anything — the
  // watchdog resets the board, and setup() above turns the pump off. This is
  // the last line of defence, and the only one that survives the logic in
  // this file being wrong.
  esp_task_wdt_config_t wdt = {
      .timeout_ms = 10000,
      .idle_core_mask = 0,
      .trigger_panic = true,
  };
  esp_err_t wdtErr = esp_task_wdt_reconfigure(&wdt);
  if (wdtErr == ESP_OK) wdtErr = esp_task_wdt_add(NULL);
  watchdogArmed = (wdtErr == ESP_OK);
  if (!watchdogArmed) {
    Serial.printf("WARNING: the watchdog did not arm (%s). The board will not "
                  "reset itself if this loop hangs.\n",
                  esp_err_to_name(wdtErr));
  }

  Serial.println();
  Serial.printf("Plant Watering Buddy %s\n", FIRMWARE_VERSION);
  if (!cfg.calibrated) {
    Serial.println("Not calibrated yet, so the pump is disabled.");
    Serial.println("Type `help` to get started.");
  } else {
    Serial.println("Type `help` for commands.");
  }
}

void loop() {
  if (watchdogArmed) esp_task_wdt_reset();

  updateButton();
  pollSerial();

  if (millis() - lastSampleAt >= SAMPLE_INTERVAL_MS) {
    lastSampleAt = millis();
    addSample(analogRead(MOISTURE_PIN));
  }

  if (evDoubleClick) {
    Serial.println("Double-click.");
    clearLockout();
  }
  if (evClick) {
    printStatus();
  }

  runControl();
  updateLed();
}
