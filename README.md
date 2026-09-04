# Plant Watering Buddy

A small, self-contained automatic plant waterer: an **M5Stack Atom Lite**
(ESP32) reads a soil moisture sensor and runs a little pump when the plant
needs water. No server, no cloud account, no phone app. It needs a USB
power supply and a jar of water.

> **This is a learning project, and it is partway through bring-up.**
> Getting familiar with the M5Stack ecosystem and ESP32 development is as
> much the point as watering a plant, so things are explained here that a
> seasoned embedded developer would not need explained.
>
> One kit is in hand. The board, the upload path and the pump are proven
> on real hardware; the sensor side and the watering firmware itself have
> not been run yet, and several numbers are still reasoned from datasheets
> rather than measured. See **Status** below for exactly which, and
> [docs/bring-up.md](docs/bring-up.md) for the procedure that settles
> them.

## Hardware

- [M5Stack Atom Lite](https://docs.m5stack.com/en/core/ATOM%20Lite) — ESP32-PICO-D4, one RGB LED, one button
- [M5Stack Watering Unit (U101)](https://docs.m5stack.com/en/unit/watering) — capacitive moisture sensor and a 5V pump on one wand
- A USB-C cable and any normal phone charger

The two connect with the Grove cable that comes with the unit. Full
pinout and wiring in [docs/hardware.md](docs/hardware.md).

## Status

Work through **[docs/bring-up.md](docs/bring-up.md)** with a kit to move
things from the second list to the first. Measurements go in
[docs/hardware.md](docs/hardware.md), one section per kit.

**Proven on real hardware** (Kit 1):

- Toolchain, board and upload path — `01_blink` runs, LED and button
  behave as expected.
- The pump runs off `PUMP_EN` on G26, and its flow rate is measured.
- No brownout through the Atom's own 5V pin across three full ten-second
  runs, on laptop USB power.

**Still assumption, not measurement:**

- **The sensor side is entirely unverified.** ADC direction, what real
  soil actually reads, and the plausibility bounds (raw 100-4000) are all
  from datasheets. The bounds in particular decide whether a healthy probe
  gets mistaken for a broken cable.
- **The watering firmware has never run.** `plant_watering_buddy` compiles
  and has been reasoned through carefully, but no soil has been watered by
  it.
- The soak time (20 minutes) and thresholds (30% / 55%) are starting
  guesses, meaningless until the soil range is known.
- The brownout result is only for laptop USB power, not the wall charger a
  finished kit would run on — see [docs/hardware.md](docs/hardware.md).
- Without a real-time clock the device cannot tell how long it was
  unpowered, so the daily dose count is deliberately kept across reboots.
  It can only make it water less than it should, never more. That one is a
  design choice rather than an open question.

## Getting started

Building a kit from the box? → **[docs/user-guide.md](docs/user-guide.md)**
is the whole path: flash, measure, calibrate, assemble, and what to do day
to day once it is running.

Just want the toolchain working? → **[docs/mac-setup.md](docs/mac-setup.md)**
gets you from nothing to a blinking LED.

Then work up through the sketches in order. Each is standalone and teaches
one thing — but they are also a measurement pipeline, not four unrelated
exercises:

| Sketch | What it proves |
|---|---|
| [`01_blink`](sketches/01_blink) | Toolchain works; LED and button respond. No libraries. |
| [`02_pump_pulse`](sketches/02_pump_pulse) | **Measures the flow rate**, and answers the brownout question by counting boots and reading the reset reason. |
| [`03_read_sensor`](sketches/03_read_sensor) | What the sensor actually reads, which direction it goes, and how noisy it is. Never runs the pump. |
| [`plant_watering_buddy`](sketches/plant_watering_buddy) | The real thing. Pulse-and-soak control, calibration in flash, serial console. |

**02 and 03 exist to produce numbers that the real firmware needs.** The
flow rate from `03` becomes its dose size; the sensor readings from `02`
become its wet and dry reference points. Those numbers are **typed into
the running device over its serial console** — `cal dry`, `cal wet`,
`set pulse <n>`, then `save` — and kept in the ESP32's flash, where they
survive reflashing. They are not compiled in, because they are properties
of one sensor in one pot and are meant to differ between kits.

Until it has them, `plant_watering_buddy` shows a yellow LED and refuses
to run the pump at all, on the grounds that an uncalibrated dose is a
guessed dose. So the ladder is not optional ceremony — skipping to the
last sketch gets you a device that will not water anything.

The pump comes before the sensor deliberately, even though meeting the
pump second sounds like the bolder order. Whether the pump browns out the
board decides how *both* kits get wired, and that answer is wanted before
anything is assembled — whereas the sensor step needs a pot, damp soil and
an hour of waiting. `02` is done at a sink in half an hour and settles the
question.

[docs/bring-up.md](docs/bring-up.md) is the procedure for all of it, and
its Step 5 covers the one case where a measurement *does* belong in the
source rather than in flash.

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
LICENSE         MIT
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

## License

[MIT](LICENSE). Do what you like with it; there is no warranty, which
matters more than usual here — this is firmware that switches a pump next
to a houseplant, and it has not been tested on hardware. Satisfy yourself
that it behaves before leaving it running unattended.
