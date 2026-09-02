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

## The power question — unresolved until hardware arrives

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

    Browns out through the Atom's 5V pin?  yes / no
    Charger used:  ____        (date: ____ , kit: ____ )

## Unknown: flow rate

M5 do not publish one. Since the whole safety design depends on knowing
how much water a given number of seconds of pumping delivers, measure it:
run the pump into a measuring jug for a timed 30 seconds and do the
arithmetic. Record the answer here.

    Measured flow rate: ____ ml per second   (date: ____ , kit: ____ )

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
