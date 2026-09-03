# Plant Watering Buddy

A small, self-contained automatic plant waterer: an **M5Stack Atom Lite**
(ESP32) reads a soil moisture sensor and runs a little pump when the plant
needs water. No server, no cloud account, no phone app. It needs a USB
power supply and a jar of water.

> **This is a learning project, and none of the code has been run yet.**
> Getting familiar with the M5Stack ecosystem and ESP32 development is as
> much the point as watering a plant, so things are explained here that a
> seasoned embedded developer would not need explained.
>
> The hardware has not arrived. Everything compiles, and nothing has been
> tested on a real device — pin assignments, thresholds, timings and the
> power budget are all reasoned from datasheets rather than measured.
> Treat every hardware-facing number as an assumption until
> [docs/bring-up.md](docs/bring-up.md) has been worked through.

## Hardware

- [M5Stack Atom Lite](https://docs.m5stack.com/en/core/ATOM%20Lite) — ESP32-PICO-D4, one RGB LED, one button
- [M5Stack Watering Unit (U101)](https://docs.m5stack.com/en/unit/watering) — capacitive moisture sensor and a 5V pump on one wand
- A USB-C cable and any normal phone charger

The two connect with the Grove cable that comes with the unit. Full
pinout and wiring in [docs/hardware.md](docs/hardware.md).

## Status

Specifically, what is still guesswork. When a kit arrives, work through
**[docs/bring-up.md](docs/bring-up.md)** — the checklist that turns these
into measurements, with blanks to fill in as you go.

- **Every timing value is a guess until the pump is measured.** The dose is
  expressed in seconds of pumping, so it means nothing until someone knows
  how many millilitres a second of pumping delivers. Run
  [`03_pump_pulse`](sketches/03_pump_pulse) first of all when hardware
  arrives — it prints the `set pulse` value to use, and settles the
  brownout question at the same time.
- Pin order, ADC direction, and button behaviour are all from datasheets,
  not from a working device.
- The plausibility bounds (raw 100-4000) and the soak time (20 minutes)
  are reasonable starting guesses, not measurements. Both want narrowing
  once a real sensor has been seen in real soil.
- The power budget question in [docs/hardware.md](docs/hardware.md) is open.
- Without a real-time clock the device cannot tell how long it was
  unpowered, so the daily dose count is deliberately kept across reboots.
  It can only make it water less than it should, never more.

## Getting started

Building a kit from the box? → **[docs/user-guide.md](docs/user-guide.md)**
is the whole path: flash, measure, calibrate, assemble, and what to do day
to day once it is running.

Just want the toolchain working? → **[docs/mac-setup.md](docs/mac-setup.md)**
gets you from nothing to a blinking LED.

Then work up through the sketches in order. Each is standalone and teaches
one thing:

| Sketch | What it proves |
|---|---|
| [`01_blink`](sketches/01_blink) | Toolchain works; LED and button respond. No libraries. |
| [`02_read_sensor`](sketches/02_read_sensor) | What the sensor actually reads, which direction it goes, and how noisy it is. Never runs the pump. |
| [`03_pump_pulse`](sketches/03_pump_pulse) | **Measures the flow rate**, and answers the brownout question by counting boots and reading the reset reason. |
| [`plant_watering_buddy`](sketches/plant_watering_buddy) | The real thing. Pulse-and-soak control, calibration in flash, serial console. |

Build any of them the same way:

```bash
cd sketches/01_blink
arduino-cli compile --profile atomlite
arduino-cli upload --profile atomlite -p /dev/cu.usbserial-XXXXXXXX
```

To check everything still compiles before pushing:

```bash
./tools/build-all.sh
```

CI runs that same script, one sketch per job, with `--strict` so that
warnings fail the build. Note that arduino-cli caches builds and a cached
build reports no warnings even when there are some, so pass `--clean` when
you want to trust the warning count.

## How the watering actually works

The obvious design — run the pump until the sensor reads wet — floods
plants. The probe sits several centimetres down, and water takes minutes
to reach it, so the sensor keeps reporting "dry" long after plenty of
water has gone in.

So instead the device delivers a small measured dose, then **ignores the
sensor entirely** for a soak period while the water works its way down.
Only then does it take a fresh reading and decide whether more is needed.

Everything else is about refusing to water when something is wrong:

| Failure | What catches it |
|---|---|
| Empty jar, kinked tube, probe out of the soil | Several doses with no measured rise in moisture → lockout |
| Probe unplugged or broken | An implausible reading is treated as a wiring fault, not as dry soil |
| Device never calibrated | Refuses to run the pump at all |
| A bug in the logic here | Hard ceiling on one pump activation, enforced independently of the state machine, plus a cap on doses per day |
| Main loop hangs | Hardware watchdog resets the board; the first two lines of `setup()` turn the pump off |

Refusing to water is always the safe failure. A plant survives a dry week;
a carpet does not survive a litre of water.

Two physical failures the firmware **cannot** see, both covered in the user
guide: a water jar sitting higher than the pot will siphon itself empty
through the tube with the pump off, and an outlet tube delivering water far
from the probe means the sensor never registers the water that arrives.

See [docs/led-and-buttons.md](docs/led-and-buttons.md) for what the light
means.

## Repo layout

```
sketches/       one directory per sketch, each with a pinned sketch.yaml
docs/           setup, bring-up checklist, hardware reference, LED key,
                user guide
tools/          build-all.sh — compile everything, same as CI does
.github/        CI: compiles every sketch on every push
```

## How the versions are pinned

Every sketch carries a `sketch.yaml` naming the exact ESP32 core and
library versions it builds against. `--profile atomlite` uses those
versions and ignores whatever else is installed locally, so every machine
and CI all produce the same binary. When two machines disagree about
behaviour, check this first.

Building without `--profile`, or in the Arduino IDE, uses whatever happens
to be installed instead. That is fine for experimenting, but it is the
first thing to rule out when something behaves differently in two places.

## Working agreement

Two identical kits, developed in parallel.

- `main` always compiles and is always safe to flash. CI enforces the
  first half; the second half is a matter of care.
- Small pull requests, even for solo work — they are the log of why
  something changed.
- Split of concerns, so nobody rewrites anyone else's `loop()`:
  **control loop, failsafes and persistence** on one side; **LED states,
  serial console, calibration UX and docs** on the other.
- Anything measured against real hardware goes in
  [docs/hardware.md](docs/hardware.md) with the date and which kit it came
  from. Two kits will not agree exactly, and knowing which numbers came
  from where matters.
