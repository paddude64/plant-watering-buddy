/*
  01_blink — first contact with the Atom Lite
  ===========================================

  WHAT THIS TEACHES
    - That your toolchain works end to end: edit, compile, upload, see a result.
    - Where the Atom Lite's two pieces of built-in hardware live:
      the RGB LED (GPIO 27) and the button under the top face (GPIO 39).
    - How to read the serial monitor, which is how you will debug
      everything from here on.

  WHAT IT DOES
    The LED cycles red -> green -> blue, one second each.
    Hold the button and it turns white for as long as you hold it.
    Every state change is printed to serial at 115200 baud.

  WHAT TO LOOK FOR
    1. The LED actually changes colour. If it doesn't, the upload didn't
       take — check the port, and see docs/mac-setup.md.
    2. Pressing the button turns the LED white immediately. If it doesn't,
       you are reading the wrong pin, or the board isn't an Atom Lite.
    3. Serial output appears. Open it with:
         arduino-cli monitor -p /dev/cu.usbserial-XXXX -c baudrate=115200
       If the text is garbled, the baud rate is wrong.

  NO LIBRARIES ARE USED HERE. That is deliberate — everything below is
  either standard Arduino or built into the ESP32 core, so there is no
  library behaviour hiding between you and the hardware.
*/

// ---------------------------------------------------------------------------
// Hardware pins. These are fixed by how the Atom Lite is wired internally;
// they are not a choice we get to make.
// ---------------------------------------------------------------------------

const int LED_PIN = 27;  // single SK6812 "NeoPixel" RGB LED
const int BTN_PIN = 39;  // the button, wired to ground when pressed

// GOTCHA WORTH KNOWING: GPIO 39 is one of the ESP32's input-only pins, and
// it has no internal pull-up resistor. INPUT_PULLUP would compile but would
// not work. The Atom Lite provides an external pull-up on the board, so
// plain INPUT is correct here: the pin reads HIGH when the button is
// released and LOW when it is pressed.

const int BRIGHTNESS = 30;  // 0-255. Keep this low — the LED is genuinely
                            // dazzling at full power, and it draws current.

void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT);

  // Give the USB serial connection a moment to come up, otherwise the first
  // few lines vanish before the monitor has attached.
  delay(500);
  Serial.println();
  Serial.println("01_blink — Atom Lite is alive.");
  Serial.println("Hold the button to force the LED white.");
}

// rgbLedWrite(pin, red, green, blue) is built into the ESP32 Arduino core
// (version 3.x and later). It handles the fussy timing the SK6812 needs.
// (Older code and tutorials call this neopixelWrite(); that name still
// works but is deprecated, and compiling with --warnings all will say so.)
void setLed(uint8_t r, uint8_t g, uint8_t b, const char *name) {
  rgbLedWrite(LED_PIN, r, g, b);
  Serial.printf("LED -> %s\n", name);
}

bool buttonPressed() {
  return digitalRead(BTN_PIN) == LOW;  // LOW means pressed, see note above
}

// Wait for `ms` milliseconds, but bail out early if the button goes down, so
// the button feels responsive instead of taking up to a second to react.
// This is a first taste of why delay() is a problem in real firmware —
// sketch 04 replaces this pattern entirely.
bool waitOrButton(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    if (buttonPressed()) return true;
    delay(10);
  }
  return false;
}

void loop() {
  const uint8_t colours[3][3] = {
    {BRIGHTNESS, 0, 0},
    {0, BRIGHTNESS, 0},
    {0, 0, BRIGHTNESS},
  };
  const char *names[3] = {"red", "green", "blue"};

  for (int i = 0; i < 3; i++) {
    setLed(colours[i][0], colours[i][1], colours[i][2], names[i]);

    if (waitOrButton(1000)) {
      setLed(BRIGHTNESS, BRIGHTNESS, BRIGHTNESS, "white (button held)");
      while (buttonPressed()) delay(10);
      Serial.println("button released");
    }
  }
}
