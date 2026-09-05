# Bring-up checklist

The first session with a real kit: prove the hardware, measure the two
things the firmware cannot guess, and get it watering under supervision.

Budget about two hours, most of it waiting for soil to do things.

> **This page is the procedure, and it stays blank.**
> Do not write your numbers here. Every kit gets its own results section
> in [hardware.md](hardware.md) — open that alongside this and fill it in
> as you go. Keeping the two apart means two people can work through this
> on two kits without overwriting each other.

**Do the whole thing per kit, from scratch.** Two pumps and two sensors
will not give the same numbers, and firmware calibrated against one kit
is wrong for the other. Somebody else's results are not a shortcut.

## Before you start

On the bench: the controller, the watering unit, a USB-C cable, **the
charger you actually intend to leave it running on**, a jug of water, an
empty measuring jug, and a sink.

Not yet: the plant.

> Steps 1-2 happen at a sink. The pump moves real water and the point of
> this session is to find out what it does before it does it near a plant.

### How these sessions work

Every step flashes a sketch, then talks to it the same way: open a serial
connection, type a command straight into that same terminal window, press
Enter, read the reply that appears right below it. There is nothing else
to open — the window running `arduino-cli monitor` is both where you type
and where the answers show up.

```bash
arduino-cli monitor -p /dev/cu.usbserial-XXXXXXXX -c baudrate=115200
```

Leave it running for the whole step. `Ctrl-C` closes it, which you only
need in order to free the port for the next `upload`.

---

## Step 1 — `01_blink` (5 minutes)

Proves the toolchain, the board, the cable and the upload path, with
nothing else connected. **Leave the watering unit in its bag for this.**

```bash
cd sketches/01_blink
arduino-cli compile --profile atomlite
arduino-cli upload --profile atomlite -p /dev/cu.usbserial-XXXXXXXX
arduino-cli monitor -p /dev/cu.usbserial-XXXXXXXX -c baudrate=115200
```

Find your port first with `arduino-cli board list` — run it before and
after plugging the Atom in, and the line that appears is yours.

- [ ] A new port appears when the Atom is plugged in
- [ ] Compile and upload succeed
- [ ] LED cycles red → green → blue
- [ ] Holding the button turns it white
- [ ] Serial monitor shows readable text

**→ Record in [hardware.md](hardware.md):** the port name.

**If the port never appears:** try another cable first — plenty are
charge-only. Then the CH9102F driver, per [mac-setup.md](mac-setup.md).

**If the upload fails with an `esptool` traceback** ending `StopIteration`
or "chip stopped responding", that is the upload speed — see
[mac-setup.md](mac-setup.md). Every `sketch.yaml` here already pins
`UploadSpeed=115200` for this reason.

Do not continue until this all works. Everything after it assumes the
board and the upload path are known-good.

---

## Step 2 — `02_pump_pulse` (30 minutes, at the sink)

Two questions at once: how much water per second, and does the pump brown
out the board. Do this before Step 3, because the answer decides how the
kits get wired, and you do not want to assemble anything twice.

Connect the watering unit to the Grove port. Inlet tube in the jug of
water, **outlet tube into the empty measuring jug**.

```bash
cd sketches/02_pump_pulse
arduino-cli compile --profile atomlite
arduino-cli upload --profile atomlite -p /dev/cu.usbserial-XXXXXXXX
arduino-cli monitor -p /dev/cu.usbserial-XXXXXXXX -c baudrate=115200
```

### First, a baseline boot number

Type `status`. Among other things it prints:

```
  boot number      : 1
  this boot's cause: power on
```

The boot number counts up on every reset, **including one you did not ask
for** — which is how a brownout gets caught later in this step. Note it
now so you have something to compare against.

Read it with `status` rather than trying to catch it at startup. `upload`
resets the board itself the instant flashing finishes, well before
`monitor` has started, so the sketch's own startup banner is reliably
gone by the time you are watching. What you will actually see on connect
is a few lines of ESP32 boot-ROM text — `ets Jun 8 2016`, `rst:`,
`configsip:` — and then nothing. That is the chip, not the sketch, and it
is normal.

- [ ] Baseline boot number noted
- [ ] Hold **the top-face button** until water comes out of the outlet —
      this is priming, and that water does not count. There is nothing to
      press on the watering unit itself; the button is always on the
      controller.
- [ ] Measuring jug emptied after priming

### Flow rate

Three times: `run 10` (runs the pump for ten seconds), read the jug, then
`ml <n>` with what you measured. The sketch does the arithmetic and prints
both the flow rate and the `set pulse` value to use.

- [ ] Three runs done, flow rates recorded

**→ Record in [hardware.md](hardware.md):** all three runs, the average,
and the suggested `set pulse`.

**If the pump does not run at all:** the pin assignment is wrong, or the
Grove cable is not seated. Yellow is `PUMP_EN` on G26.

**If the numbers vary a lot between runs:** normal for a pump this small,
especially the first run after priming. Average them, and lean towards the
*higher* ml/s figure when setting the dose, since that errs towards less
water.

### Running it dry

This step runs the pump dry more than any other, and that is fine within
limits, so it is worth knowing where the limits are.

**Short bursts are fine.** Priming pulls air through until the water
arrives, and purging for storage deliberately pushes the water out and
draws air behind it. Neither hurts anything.

**Sustained dry running is what damages a small pump**, because the water
it moves is also what carries heat away from the motor and the head. So
keep dry running to seconds, not minutes.

Two ways to run it dry here without meaning to:

- The inlet tube lifting out of the jug, or the jug emptying, part way
  through a `run 10`.
- Priming and then getting distracted — the button runs the pump for as
  long as you hold it, up to the sketch's twenty-second ceiling.

Listen while it runs. A pump moving water and a pump moving air do not
sound the same — the note usually rises and the gurgling stops. Once you
have heard the difference, an empty jug is obvious across the room, which
is more useful later than it sounds now.

**In normal service this is already bounded.** If the reservoir empties,
the firmware gives up after three doses that produce no change in the
soil — around nine seconds of dry running in total, spread over about an
hour — and then locks out rather than continuing to pump at an empty jar.
That is comfortably inside what the pump tolerates.

The case nobody has observed yet is a jar that is *nearly* empty and
sucking air intermittently rather than cleanly running out. That could
add up to more dry running than a clean empty, spread over several
cycles. If you see it happen, note what it did — it is the sort of thing
worth knowing before deciding whether the firmware needs to care.

### Brownout

A brownout is the 5V rail sagging when the pump motor starts, far enough
that the ESP32's own detector forces a reset to protect itself. It matters
because it is invisible — the board is back in a few hundred milliseconds
— and because it imitates a plumbing fault: doses get cut short, the soil
never gets wetter, and the real firmware eventually locks out blaming the
water jar.

The sketch counts brownouts and keeps the tally in flash, so `status`
reports them however long ago they happened.

#### On laptop power

- [ ] `status` after the runs — `brownouts so far` should read 0

#### On the charger it will actually live on

This is the test that counts, and it is the one people skip. A laptop USB
port and a phone charger do not supply the same current, and it is the
charger that will be plugged in at the plant for months.

The Atom Lite has one USB-C port carrying both power and data, so **you
cannot watch over serial while it runs on a charger** — which is exactly
why the tally is kept in flash rather than just printed at boot.

1. `reset` — zeroes the counters, so this charger gets a clean test
2. Unplug from the laptop, plug into the wall charger
3. **Hold the button** to run the pump. No serial needed. Do it several
   times, and watch the LED: if the board browns out it resets and comes
   straight back up flashing **red three times**, which is the fault code
   for "brownouts recorded" — see
   [led-and-buttons.md](led-and-buttons.md). That is the brownout
   happening in front of you, with no laptop involved
4. Unplug, plug back into the laptop, `status`

- [ ] `brownouts so far` after the charger test: `______`

Anything above zero means that charger cannot start this pump through the
Atom.

> Do not try to read `this boot's cause` for this — it only ever describes
> the *most recent* reset, and that will be you plugging into the laptop.
> It will say `power on` however badly the charger performed. The tally is
> the only part that survives the trip.

**→ Record in [hardware.md](hardware.md):** the brownout count for each
charger tested, and **which chargers those were**.

**If there were brownouts:** the pump is starving the board through the
Atom's 5V pin. Wire the pump's red 5V wire directly to the USB supply,
ground common — [hardware.md](hardware.md) — then repeat this step.
**Wire both kits the same way**, whichever way it turns out.

**If it browns out only with one charger:** use the better one, and note
which. Do not ship a kit with a charger that browns out.

---

## Step 3 — `03_read_sensor` (an hour, mostly waiting)

What the sensor actually reads. The pump is never used by this sketch, so
nothing can get wet unexpectedly.

```bash
cd sketches/03_read_sensor
arduino-cli compile --profile atomlite
arduino-cli upload --profile atomlite -p /dev/cu.usbserial-XXXXXXXX
arduino-cli monitor -p /dev/cu.usbserial-XXXXXXXX -c baudrate=115200
```

No typed commands here — it streams readings on its own, and a **button
click** captures one as a labelled reference. Take four:

- [ ] Probe clean and dry, in open air
- [ ] Probe standing in a glass of water
- [ ] Probe pushed into dry soil in the actual pot
- [ ] Same pot, 30 minutes after watering it thoroughly

**→ Record in [hardware.md](hardware.md):** all four readings, the jitter
figure, and whether wetter reads higher or lower.

**Direction does not matter.** The firmware works it out from the
calibration points, so there is nothing to configure and nothing to get
backwards.

**If any real reading lands near 100 or 4000**, those are the bounds the
firmware treats as a broken cable. That is a change to the sketch, not a
setting — see Step 5.

### The one that decides everything: how wide is the soil range?

Compare two spans: air → water, and dry soil → wet soil.

If the soil spread is a decent fraction of the air/water spread, calibrate
with air and water in Step 4 and the default thresholds are a reasonable
start.

**If the soil spread is much narrower** — say a fifth of it — then
calibrating against air and water squashes every real reading into a
narrow band of the percentage scale, and thresholds of 30% and 55% will
never be crossed. The device would sit at 45% forever and never water
anything. In that case calibrate against the *soil* endpoints instead:
`cal dry` with the probe in the dry pot, `cal wet` in the same pot right
after watering. The scale then spans the range the device actually lives
in, and the thresholds mean something.

- [ ] Decided which pair to calibrate against

---

## Step 4 — the real firmware

```bash
cd sketches/plant_watering_buddy
arduino-cli compile --profile atomlite
arduino-cli upload --profile atomlite -p /dev/cu.usbserial-XXXXXXXX
arduino-cli monitor -p /dev/cu.usbserial-XXXXXXXX -c baudrate=115200
```

LED should be **yellow** — not calibrated, so the pump is disabled. Type
each of these into that same monitor window:

- [ ] `cal dry` — with the probe wherever Step 3 decided
- [ ] `cal wet` — likewise
- [ ] `set pulse <n>` — the number Step 2 gave you
- [ ] `save` — without this it is all lost on reboot
- [ ] `status` looks right, LED now **blue and breathing**

### Supervised first cycle — do not skip

Assemble at the plant per [user-guide.md](user-guide.md), then force one
dose with `water` and watch the whole thing.

- [ ] Water actually reaches the soil
- [ ] It lands **near the probe**, not in the far corner of the pot
- [ ] LED goes green, then cyan
- [ ] The jar is **lower than the pot** (or it will siphon itself empty)
- [ ] Come back after the soak and check the reading moved

**→ Record in [hardware.md](hardware.md):** moisture before the dose and
after the soak, and the settings this kit ended up with.

**If the reading barely moved:** either the dose is too small, or the
water is not reaching the probe. Fix the tube position before touching the
dose — the firmware will lock out after three unresponsive doses, which is
it working correctly, not a bug.

---

## Step 5 — where the numbers actually go

Three different destinations, and mixing them up is the easy mistake.

### 1. Into the device, over serial — most of them

Calibration and settings live in the device's own flash, set with the
commands in Step 4 and kept by `save`. **These are per-device and are
meant to differ between kits** — different sensor, different soil,
different pot. They are never committed to the repo.

    cal dry / cal wet      set pulse     set soak
    set below / set above  set maxday

Reflashing does not erase them. `status` shows what a device currently
holds.

**Can you skip the serial console and just edit them into the sketch?**
Yes — the `Settings cfg` initialiser at the top of
`plant_watering_buddy.ino` is where those values start from, so filling in
`dryRaw`, `wetRaw`, `pulseSeconds` and setting `calibrated = true` there
produces a device that comes up already calibrated. Stored settings still
win over it, so a device that has ever been `save`d keeps what it was
told.

It is a reasonable shortcut for one device you are setting up yourself.
It is a bad habit for this repo, because those values describe *one*
sensor in *one* pot: committing yours makes the sketch wrong for the
other kit, and the two would need branches or `#ifdef`s to coexist —
more trouble than typing three commands. If you do edit them locally to
save yourself the serial session, do not commit it.

### 2. Into the sketch source — only if a *shared* assumption was wrong

These are compile-time constants, identical on both kits, and changing one
means editing the code and committing it:

| What | Where | Change it if |
|---|---|---|
| `SENSOR_RAW_MIN` / `SENSOR_RAW_MAX` | `plant_watering_buddy.ino` | Real soil readings come near 100 or 4000, so a healthy probe could trip a false "broken cable" fault |
| `Settings cfg` defaults (`soakMinutes`, `waterBelowPct`, `stopAbovePct`, `maxPulsesPerDay`) | same file | Experience shows the starting values are simply bad, so the *next* person should start closer to right |
| `SUGGESTED_DOSE_ML` | `02_pump_pulse.ino` | 25 ml turns out to be the wrong ballpark for a houseplant |

Say why in the commit message — a threshold changed without a reason
is indistinguishable from a typo six months later.

- [ ] Anything this session proved wrong, changed in the sketch and
      committed

### 3. Into the docs — the measurements themselves

- [ ] Results written into [hardware.md](hardware.md) under this kit
- [ ] Anything the docs got wrong, fixed — especially the assembly steps
      in [user-guide.md](user-guide.md), which were written from
      M5Stack's documentation rather than from a kit in hand

## What will probably need changing

Based on what is currently a guess rather than a measurement:

- **Soak time (20 min)** — too short and it doses again before the water
  has arrived; too long and it is sluggish. Watch how long the reading
  actually takes to settle in Step 3 and set it from that.
- **Thresholds (30% / 55%)** — placeholders, and meaningless until the
  soil range is known.
- **Plausibility bounds (100–4000)** — chosen to catch an unplugged probe,
  never tested against a real one.
- **Dose size (25 ml suggested)** — a guess at what a houseplant wants.
  Depends entirely on the pot.
