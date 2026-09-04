# Hardware notes

Two parts, one cable between them.

## M5Stack Atom Lite

ESP32-PICO-D4. Powered over USB-C, rated **5V @ 500mA**.

| What | GPIO | Notes |
|---|---|---|
| RGB LED (SK6812) | 27 | One pixel. `rgbLedWrite(27, r, g, b)` in ESP32 core 3.x — no library needed. |
| Button | 39 | The entire top face. Reads LOW when pressed. Input-only pin with an external pull-up: use `pinMode(39, INPUT)`, **not** `INPUT_PULLUP`. |
| Grove port | 26, 32 | Also usable as I2C (SDA 25 / SCL 21) but we use them as plain GPIO/ADC. |

Reset is the small separate button on the side.

## M5Stack Watering Unit (U101)

Capacitive moisture sensor plus a pump on one wand, sharing a single
4-pin HY2.0 Grove cable.

| Wire | Signal | Atom Lite pin |
|---|---|---|
| Black | GND | GND |
| Red | 5V | 5V |
| Yellow | `PUMP_EN` | **GPIO 26** — drive HIGH to run the pump |
| White | Analog out | **GPIO 32** — moisture reading via ADC |

The sensor is capacitive rather than resistive, which is the good kind:
no exposed electrodes corroding away in wet soil over months.

## The power question

M5 rate the pump at **5W**. At 5V that is up to 1A, against the Atom
Lite's 500mA input rating. In practice
[at least one project](https://github.com/rasclatt-dot-com/ESP32-Plant-Waterer-for-ESPHome)
runs the pump straight off the Atom's Grove 5V pin without trouble, given
a decent USB supply.

So it may well be fine. But if the Atom resets or the LED flickers the
moment the pump starts, that is a brownout, and the fix is to stop routing
pump current through the Atom: connect the Watering Unit's red 5V wire
directly to the USB supply, keeping ground common between the two.

**This must be settled before the second kit is built**, so that both kits
are wired identically and the same firmware behaves the same way in two
different houses.

[`03_pump_pulse`](../sketches/03_pump_pulse) answers it definitively rather
than by impression. A brownout is easy to miss — the board reboots so fast
it just looks like the pump stuttered — so that sketch keeps a boot counter
in flash and prints `esp_reset_reason()` on every startup. If the boot
number climbs while the pump runs and the cause reads `BROWNOUT`, that is
the answer. Test with the charger you actually intend to leave it on.

Results per kit are below.

## Flow rate

M5 do not publish one, and the whole safety design depends on knowing how
much water a given number of seconds of pumping delivers — so it has to be
measured on each kit. Results per kit are below.

`03_pump_pulse` measures this for you: prime the tube, `run 10`, then tell
it how many millilitres landed in the jug with `ml <n>`. It prints the flow
rate and the `set pulse` value to use. Run it a few times and average — a
pump this small is not very repeatable.

Then set the dose on each device over serial, from how much water you
actually want per dose:

    pulse seconds  =  desired dose in ml  /  measured ml per second

A sensible dose is a splash, not a drink — something like 20-30ml for a
normal houseplant pot, repeated after a soak if the soil is still dry.
Enter it with `set pulse <seconds>` then `save`. The firmware refuses
anything over 15 seconds regardless.

## ADC readings

The ESP32's ADC is 12-bit, so raw readings land somewhere in 0–4095. The
useful range in practice is narrower and varies unit to unit, which is
exactly why calibration has to happen on each device rather than being
baked into the source. Values at the extremes (0, or pinned near 4095)
should be treated as "sensor probably disconnected", not as real soil
readings.

---

# Measured results, per kit

Filled in by working through [bring-up.md](bring-up.md). **Each kit gets
its own block** — two pumps and two sensors will not agree, and a number
from one kit is not a substitute for measuring the other.

Calibration and settings (`cal dry`, `set pulse`, and the rest) live in
each device's own flash, not here. What is recorded here is the
*measurements* — the evidence behind those settings, and the answers to
the two questions the firmware cannot work out for itself.

## Kit 1

    Date:        2026-09-04
    Serial port: /dev/cu.usbserial-8152483DF5

**Step 1 — `01_blink`:** passed. LED cycle, button override and serial
output all confirmed on real hardware.

**Step 2 — flow rate:**

| Run | Collected | Pump ran for | Rate |
|---|---|---|---|
| 1 | 100 ml | 10.627 s | 9.4 ml/s |
| 2 | 100 ml | ~10 s (not captured) | ~10 ml/s |
| 3 | 100 ml | 10.159 s | 9.8 ml/s |

    Average:        ~9.6 ml/s
    Dose setting:   set pulse 3      (for a 25 ml dose)

**Step 2 — brownout:** no brownout. Boot number held at 1, cause
`power on`, across three consecutive full 10-second runs.

    Charger tested:   MacBook USB-C port (the same port used for
                      programming)
    Still outstanding: the USB-C wall charger the finished kit will
                      actually run on. A laptop port and a wall charger do
                      not always supply the same current, so the wiring
                      question is not closed until that is retested.

**Step 3 — sensor:** not done yet.

| Where the probe is | Reading |
|---|---|
| Clean and dry, in open air |  |
| Standing in a glass of water |  |
| Dry soil in the pot |  |
| Same pot, 30 min after watering |  |

    Jitter in steady conditions:  ________ counts
    Wetter reads:                 higher / lower
    Calibrated against:           air/water  /  dry soil/wet soil

**Step 4 — first cycle:** not done yet.

    Moisture before dose: ________%    after soak: ________%
    Settings: set pulse ____  set soak ____  set below ____
              set above ____  set maxday ____

## Kit 2

    Date:        ____________
    Serial port: ____________

**Step 1 — `01_blink`:**

**Step 2 — flow rate:**

| Run | Collected | Pump ran for | Rate |
|---|---|---|---|
| 1 |  |  |  |
| 2 |  |  |  |
| 3 |  |  |  |

    Average:        ________ ml/s
    Dose setting:   set pulse ________

**Step 2 — brownout:**

    Boot number before / after:  ________ / ________
    Reset cause:                 ________
    Charger tested:              ________

**Step 3 — sensor:**

| Where the probe is | Reading |
|---|---|
| Clean and dry, in open air |  |
| Standing in a glass of water |  |
| Dry soil in the pot |  |
| Same pot, 30 min after watering |  |

    Jitter in steady conditions:  ________ counts
    Wetter reads:                 higher / lower
    Calibrated against:           air/water  /  dry soil/wet soil

**Step 4 — first cycle:**

    Moisture before dose: ________%    after soak: ________%
    Settings: set pulse ____  set soak ____  set below ____
              set above ____  set maxday ____
