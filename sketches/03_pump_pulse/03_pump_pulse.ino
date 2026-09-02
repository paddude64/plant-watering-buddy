/*
  03_pump_pulse — measure the pump, and settle the power question
  ==============================================================

  This is the most useful sketch in the repo, because it produces the one
  number the whole safety design rests on: HOW MUCH WATER PER SECOND.

  The real firmware doses in seconds of pumping. Until you know the flow
  rate, "pump for 3 seconds" is not a dose, it is a guess — it could be a
  teaspoon or it could be a third of a mug. M5 do not publish a figure, so
  it has to be measured, on each kit, because two pumps will not match.

  ------------------------- SAFETY, READ THIS -------------------------
  This is the first sketch that moves water. It only ever runs the pump
  while a person is driving it — there is no automatic behaviour here at
  all — and it refuses to run for more than 20 seconds in one go.

  Do the measuring over a sink, with the outlet tube in a measuring jug,
  not in a plant pot.
  ---------------------------------------------------------------------

  ============================ THE EXERCISE =============================

  PART 1 — measure the flow rate

    1. Put the inlet tube in a jug of water and the outlet tube into an
       empty measuring jug.
    2. Hold the button for a few seconds to prime it: the tube has to fill
       with water before anything comes out the far end, and that priming
       water must not count towards your measurement.
    3. Empty the measuring jug. Now type:  run 10
    4. Read off how many millilitres landed in the jug, and type:  ml 45
       (or whatever the real number is).
    5. It prints your flow rate and the dose setting to use.

    Do it two or three times and take the average. A small pump like this
    is not especially repeatable, and the first run after priming is
    usually the odd one out.

  PART 2 — settle the power question

    The Atom Lite is rated 5V @ 500mA. The pump is rated 5W, which is up
    to 1A. Running the pump through the Atom's own Grove 5V pin may well
    be fine — plenty of people do it — or it may brown the board out.
    Guessing is not good enough: a board that resets mid-dose is a board
    that could reset with the pump pin in an unknown state.

    This sketch prints the reason for every boot. An ESP32 knows when it
    has browned out and says so. So:

      - Note the boot number printed at startup.
      - Run the pump a few times, with the supply you actually intend to
        leave it plugged into.
      - If the board resets, the boot number goes up and the reason will
        say BROWNOUT. That is a definite answer, not an impression.

    If it does brown out, the fix is in docs/hardware.md: take the pump's
    red 5V wire directly to the USB supply instead of through the Atom,
    keeping ground common. Then re-run this test.

    Test with the actual charger you plan to use. A weak phone charger and
    a good one behave very differently here.

  Write the results in docs/hardware.md — flow rate, which kit, and
  whether it browned out.

  COMMANDS
    run <sec>   run the pump for a number of seconds (1-20)
    ml <n>      how much came out of the last run; prints the flow rate
    status      boot count, last reset reason, last run
    help
  Holding the button runs the pump for as long as you hold it.
*/

#include <Preferences.h>
#include <esp_system.h>

const int PUMP_PIN = 26;  // Grove yellow. HIGH runs the pump.
const int LED_PIN = 27;
const int BTN_PIN = 39;

const unsigned long PUMP_HARD_MAX_MS = 20UL * 1000UL;

// A dose for a normal houseplant pot: a splash, not a drink. Used only to
// suggest a `set pulse` value for the real firmware.
const int SUGGESTED_DOSE_ML = 25;

Preferences prefs;

bool pumpRunning = false;
bool hardLimitLatched = false;  // set when the ceiling fires; cleared only by
                                // letting go of the button
unsigned long pumpStartedAt = 0;
unsigned long requestedRunMs = 0;   // 0 means "while the button is held"
unsigned long lastRunMs = 0;        // how long the last completed run lasted
int bootCount = 0;

String inputLine;

// --- button ----------------------------------------------------------------
bool btnStable = false, btnLastRaw = false;
unsigned long btnChangedAt = 0;
bool btnHeld = false;

void updateButton() {
  unsigned long now = millis();
  bool raw = (digitalRead(BTN_PIN) == LOW);
  if (raw != btnLastRaw) {
    btnLastRaw = raw;
    btnChangedAt = now;
  }
  if (now - btnChangedAt > 25) btnStable = raw;
  btnHeld = btnStable;
}

// --- pump ------------------------------------------------------------------
void pumpOn(unsigned long forMs) {
  if (!pumpRunning) {
    pumpStartedAt = millis();
    pumpRunning = true;
    digitalWrite(PUMP_PIN, HIGH);
  }
  requestedRunMs = forMs;
}

void pumpOff() {
  digitalWrite(PUMP_PIN, LOW);
  if (pumpRunning) {
    lastRunMs = millis() - pumpStartedAt;
    pumpRunning = false;
    Serial.printf("Pump off after %lu.%03lu s\n", lastRunMs / 1000,
                  lastRunMs % 1000);
    if (requestedRunMs > 0) {
      Serial.printf("Now measure the jug and type:  ml <millilitres>\n");
    }
  }
  requestedRunMs = 0;
}

const char *resetReasonText(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "power on";
    case ESP_RST_SW:       return "software restart";
    case ESP_RST_PANIC:    return "CRASH (panic)";
    case ESP_RST_INT_WDT:  return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "*** BROWNOUT — not enough power ***";
    case ESP_RST_EXT:      return "external reset button";
    case ESP_RST_DEEPSLEEP:return "woke from deep sleep";
    default:               return "unknown";
  }
}

void reportFlowRate(int millilitres) {
  if (lastRunMs == 0) {
    Serial.println("No completed run to measure yet. Try `run 10` first.");
    return;
  }
  if (millilitres <= 0) {
    Serial.println("Give a positive number of millilitres.");
    return;
  }
  float seconds = lastRunMs / 1000.0f;
  float mlPerSec = millilitres / seconds;
  Serial.println();
  Serial.printf("  %d ml in %.2f s  =  %.1f ml per second\n", millilitres,
                seconds, mlPerSec);
  Serial.println();
  Serial.printf("  For a %d ml dose, the real firmware wants:\n",
                SUGGESTED_DOSE_ML);
  float doseSeconds = SUGGESTED_DOSE_ML / mlPerSec;
  int rounded = (int)(doseSeconds + 0.5f);
  if (rounded < 1) rounded = 1;
  Serial.printf("      set pulse %d        (%.1f s, rounded)\n", rounded,
                doseSeconds);
  Serial.println();
  Serial.println("  Record the flow rate in docs/hardware.md, with which kit");
  Serial.println("  it came from. Run it a couple more times and average.");
  Serial.println();
}

void printStatus() {
  Serial.println();
  Serial.printf("  boot number      : %d\n", bootCount);
  Serial.printf("  this boot's cause: %s\n",
                resetReasonText(esp_reset_reason()));
  Serial.printf("  pump             : %s\n", pumpRunning ? "RUNNING" : "off");
  if (lastRunMs) Serial.printf("  last run         : %lu ms\n", lastRunMs);
  Serial.println();
  Serial.println("  If the boot number went up while the pump was running,");
  Serial.println("  and the cause says BROWNOUT, the pump is pulling more");
  Serial.println("  than this supply can give through the Atom. See");
  Serial.println("  docs/hardware.md for the direct-to-USB wiring fix.");
  Serial.println();
}

void printHelp() {
  Serial.println();
  Serial.println("  run <sec>   run the pump for 1-20 seconds");
  Serial.println("  ml <n>      millilitres collected from the last run");
  Serial.println("  status      boot count, reset reason, last run");
  Serial.println("  stop        stop the pump now");
  Serial.println("  Hold the button to run the pump while held.");
  Serial.println();
}

void handleCommand(String line) {
  line.trim();
  if (!line.length()) return;
  String verb = line, rest = "";
  int sp = line.indexOf(' ');
  if (sp > 0) {
    verb = line.substring(0, sp);
    rest = line.substring(sp + 1);
    rest.trim();
  }
  verb.toLowerCase();

  if (verb == "help" || verb == "?") {
    printHelp();
  } else if (verb == "status") {
    printStatus();
  } else if (verb == "stop") {
    pumpOff();
  } else if (verb == "run") {
    int sec = rest.toInt();
    if (sec < 1 || sec > (int)(PUMP_HARD_MAX_MS / 1000)) {
      Serial.printf("Between 1 and %lu seconds.\n", PUMP_HARD_MAX_MS / 1000);
      return;
    }
    Serial.printf("Running the pump for %d s. Outlet tube in the jug?\n", sec);
    pumpOn((unsigned long)sec * 1000UL);
  } else if (verb == "ml") {
    reportFlowRate(rest.toInt());
  } else {
    Serial.printf("Unknown command '%s'. Try `help`.\n", verb.c_str());
  }
}

void pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (inputLine.length()) {
        handleCommand(inputLine);
        inputLine = "";
      }
    } else if (inputLine.length() < 60) {
      inputLine += c;
    }
  }
}

void setup() {
  // First thing, before anything else: pump off. If this boot is a brownout
  // reset that happened mid-run, this is what stops it running again.
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  pinMode(BTN_PIN, INPUT);

  Serial.begin(115200);
  delay(500);

  // The boot counter is the whole point of Part 2: a brownout is invisible
  // if you are not counting boots, because the board comes back so fast it
  // just looks like the pump stuttered.
  prefs.begin("pump", false);
  bootCount = prefs.getInt("boots", 0) + 1;
  prefs.putInt("boots", bootCount);

  esp_reset_reason_t reason = esp_reset_reason();

  Serial.println();
  Serial.println("03_pump_pulse");
  Serial.printf("  boot number %d, cause: %s\n", bootCount,
                resetReasonText(reason));
  if (reason == ESP_RST_BROWNOUT) {
    Serial.println();
    Serial.println("  ^^^ THERE IT IS. The pump is browning out the board.");
    Serial.println("  Wire the pump's red 5V wire straight to the USB supply");
    Serial.println("  instead of through the Atom. See docs/hardware.md.");
    Serial.println();
  }
  Serial.println("  Type `help`. Measure over a sink, not over a plant.");
  Serial.println();
}

void loop() {
  updateButton();
  pollSerial();

  // Holding the button runs the pump, for priming the tube.
  //
  // The latch matters: without it, the hard limit below stops the pump and
  // then this line starts it again on the very next iteration while the
  // button is still down, so the ceiling could be held open indefinitely.
  // Releasing the button is what re-arms it.
  if (!btnHeld) hardLimitLatched = false;
  if (btnHeld && !pumpRunning && !hardLimitLatched) pumpOn(0);
  if (!btnHeld && pumpRunning && requestedRunMs == 0) pumpOff();

  if (pumpRunning) {
    unsigned long elapsed = millis() - pumpStartedAt;
    // The timed run finishing.
    if (requestedRunMs > 0 && elapsed >= requestedRunMs) pumpOff();
    // The backstop, which applies to every route to the pump including the
    // button, and does not trust the logic above to be correct.
    else if (elapsed > PUMP_HARD_MAX_MS) {
      Serial.println("Hard limit reached — stopping. Let go of the button "
                     "before running it again.");
      hardLimitLatched = true;
      pumpOff();
    }
  }

  rgbLedWrite(LED_PIN, 0, pumpRunning ? 60 : 0, pumpRunning ? 0 : 20);
}
