/*
  03_read_sensor — what the moisture sensor actually does
  ======================================================

  WHAT THIS TEACHES
    - That the sensor gives you a raw number, not a moisture reading, and
      that the number means nothing until you have seen its range.
    - Which direction it goes. Wetter might read higher or lower — it
      depends on the sensor, and assuming is how you end up with firmware
      that waters a plant because it is already wet.
    - How noisy a single reading is, and therefore why the real firmware
      averages a rolling window instead of trusting one sample.

  WHAT IT DOES
    Prints a live reading roughly once a second: the raw value, a smoothed
    average, the highest and lowest seen so far, and how much the readings
    are jittering. The LED shows where the current reading sits within the
    range you have observed.

  ============================ THE EXERCISE =============================
  Plug the Watering Unit into the Grove port. THE PUMP IS NEVER RUN BY
  THIS SKETCH — leave the tubes out of the water if you like.

  Work through these, clicking the button at each step to capture a
  labelled reading, and write the numbers down:

    1. Probe clean and dry, held in open air.
    2. Probe standing in a glass of water, up to but not past the line
       marked on it.
    3. Probe pushed into dry soil in the actual pot.
    4. Same pot, thoroughly watered, half an hour later.

  Steps 1 and 2 are what the real firmware calls `cal dry` and `cal wet`.
  Steps 3 and 4 are the ones that matter: they tell you the range the
  device will really live in, which is always narrower than air-to-glass.

  QUESTIONS THIS SHOULD ANSWER
    - Does the number go UP or DOWN as things get wetter?
    - How far apart are dry soil and wet soil, really? If it is only a
      few dozen counts, the thresholds in the real firmware are going to
      need to be much closer together than the defaults.
    - How much does a single reading jump around when nothing is moving?
      That is the noise the averaging exists to hide.
    - Does the reading ever fall outside 100-4000? Those are the bounds
      the real firmware treats as "cable must be unplugged". If real soil
      readings get near them, they need moving.

  Record what you find in docs/hardware.md. Two kits will not agree, so
  note which one the numbers came from.
*/

const int MOISTURE_PIN = 32;  // Grove white wire
const int LED_PIN = 27;
const int BTN_PIN = 39;  // input-only, external pull-up: INPUT, not INPUT_PULLUP

// These are the bounds the real firmware uses to decide a reading is a
// wiring fault rather than soil. Shown here so you can see whether your
// actual readings come uncomfortably close to them.
const int SENSOR_RAW_MIN = 100;
const int SENSOR_RAW_MAX = 4000;

const int WINDOW = 32;
int window[WINDOW];
int windowCount = 0;
int windowIndex = 0;

int minSeen = 4095;
int maxSeen = 0;
int captureNumber = 0;

unsigned long lastSampleAt = 0;
unsigned long lastPrintAt = 0;

const unsigned long SAMPLE_MS = 250;
const unsigned long PRINT_MS = 1000;

// --- button, debounced, with a click/double-click distinction --------------
bool btnStable = false, btnLastRaw = false;
unsigned long btnChangedAt = 0, btnLastReleaseAt = 0;
bool clickPending = false;
unsigned long clickPendingAt = 0;
bool evClick = false, evDoubleClick = false;

void updateButton() {
  unsigned long now = millis();
  bool raw = (digitalRead(BTN_PIN) == LOW);
  evClick = evDoubleClick = false;

  if (raw != btnLastRaw) {
    btnLastRaw = raw;
    btnChangedAt = now;
  }
  if (now - btnChangedAt > 25 && raw != btnStable) {
    btnStable = raw;
    if (!btnStable) {  // released
      if (now - btnLastReleaseAt < 400) {
        evDoubleClick = true;
        clickPending = false;
      } else {
        clickPending = true;
        clickPendingAt = now;
      }
      btnLastReleaseAt = now;
    }
  }
  if (clickPending && now - clickPendingAt > 400) {
    clickPending = false;
    evClick = true;
  }
}

// --- readings --------------------------------------------------------------
void addSample(int v) {
  window[windowIndex] = v;
  windowIndex = (windowIndex + 1) % WINDOW;
  if (windowCount < WINDOW) windowCount++;
  if (v < minSeen) minSeen = v;
  if (v > maxSeen) maxSeen = v;
}

int average() {
  long sum = 0;
  for (int i = 0; i < windowCount; i++) sum += window[i];
  return windowCount ? (int)(sum / windowCount) : 0;
}

// The spread within the current window: how much the reading jitters when
// nothing is actually changing. This is the number that justifies averaging.
int jitter() {
  if (windowCount < 2) return 0;
  int lo = 4095, hi = 0;
  for (int i = 0; i < windowCount; i++) {
    if (window[i] < lo) lo = window[i];
    if (window[i] > hi) hi = window[i];
  }
  return hi - lo;
}

// A 40-character bar showing where this reading sits in the full ADC range.
void drawBar(int value, char *out) {
  int filled = (int)((long)value * 40 / 4095);
  for (int i = 0; i < 40; i++) out[i] = (i < filled) ? '#' : '.';
  out[40] = '\0';
}

// LED shows position within the range seen SO FAR, not absolute wetness —
// we cannot know which end is wet until you have shown it both.
void updateLed(int value) {
  int span = maxSeen - minSeen;
  if (span < 20) {
    rgbLedWrite(LED_PIN, 10, 10, 10);  // dim white: nothing learned yet
    return;
  }
  int pos = (int)((long)(value - minSeen) * 40 / span);
  if (pos < 0) pos = 0;
  if (pos > 40) pos = 40;
  rgbLedWrite(LED_PIN, 40 - pos, 0, pos);  // one end red, the other blue
}

void printCapture() {
  captureNumber++;
  int avg = average();
  Serial.println();
  Serial.println("======================================================");
  Serial.printf("  CAPTURE %d — write this one down\n", captureNumber);
  Serial.printf("    smoothed reading : %d\n", avg);
  Serial.printf("    jitter in window : %d counts\n", jitter());
  Serial.printf("    range seen so far: %d to %d (span %d)\n", minSeen, maxSeen,
                maxSeen - minSeen);
  if (avg < SENSOR_RAW_MIN || avg > SENSOR_RAW_MAX) {
    Serial.printf("    *** outside the firmware's plausible band %d-%d ***\n",
                  SENSOR_RAW_MIN, SENSOR_RAW_MAX);
  }
  Serial.println("======================================================");
  Serial.println();
}

void setup() {
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(BTN_PIN, INPUT);
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("03_read_sensor");
  Serial.println("Click the button to capture a labelled reading.");
  Serial.println("Double-click to reset the min/max range.");
  Serial.println();
  Serial.println("    raw  smoothed   jitter   min   max  bar");

  for (int i = 0; i < WINDOW; i++) addSample(analogRead(MOISTURE_PIN));
}

void loop() {
  unsigned long now = millis();
  updateButton();

  if (now - lastSampleAt >= SAMPLE_MS) {
    lastSampleAt = now;
    addSample(analogRead(MOISTURE_PIN));
  }

  if (evClick) printCapture();
  if (evDoubleClick) {
    minSeen = 4095;
    maxSeen = 0;
    Serial.println("--- min/max reset ---");
  }

  if (now - lastPrintAt >= PRINT_MS) {
    lastPrintAt = now;
    int raw = window[(windowIndex + WINDOW - 1) % WINDOW];
    int avg = average();
    char bar[41];
    drawBar(avg, bar);
    Serial.printf("%7d  %7d  %7d  %5d %5d  %s%s\n", raw, avg, jitter(), minSeen,
                  maxSeen, bar,
                  (avg < SENSOR_RAW_MIN || avg > SENSOR_RAW_MAX) ? "  <-- IMPLAUSIBLE" : "");
  }

  updateLed(average());
}
