# Bring-up checklist

The first session with real hardware. Work through it in order and write
the numbers down as you go — this page is the worksheet, and
[hardware.md](hardware.md) is where the final numbers live afterwards.

Budget about two hours, most of it waiting for soil to do things.

Do this for **each kit separately**. Two pumps and two sensors will not
give the same numbers, and firmware calibrated against one is wrong for
the other.

    Kit:  ______________        Date:  ______________

## Before you start

On the bench: the controller, the watering unit, a USB-C cable, **the
charger you actually intend to leave it running on**, a jug of water, an
empty measuring jug, and a sink.

Not yet: the plant.

> Steps 1-2 happen at a sink. The pump moves real water and the point of
> this session is to find out what it does before it does it near a plant.

---

## Step 1 — `01_blink` (5 minutes)

Proves the toolchain, the board, the cable and the upload path, with
nothing else connected. **Leave the watering unit in its bag for this.**

- [ ] `arduino-cli board list` shows a port
- [ ] Port name: `/dev/cu.________________`
- [ ] Compile and upload succeed
- [ ] LED cycles red → green → blue
- [ ] Holding the button turns it white
- [ ] Serial monitor at 115200 shows readable text

**If the port never appears:** try another cable first — plenty are
charge-only. Then the CH9102F driver, per [mac-setup.md](mac-setup.md).

Do not continue until this all works. Everything after it assumes the
board and the upload path are known-good.

---

## Step 2 — `03_pump_pulse` (30 minutes, at the sink)

Two questions at once: how much water per second, and does the pump brown
out the board. Do this before Step 3, because the answer decides how the
kits get wired, and you do not want to assemble anything twice.

Connect the watering unit to the Grove port. Inlet tube in the jug of
water, **outlet tube into the empty measuring jug**.

Flash `03_pump_pulse`, then open the serial monitor using the port name
you wrote down in Step 1:

```bash
cd sketches/03_pump_pulse
arduino-cli compile --profile atomlite
arduino-cli upload --profile atomlite -p /dev/cu.usbserial-XXXXXXXX
arduino-cli monitor -p /dev/cu.usbserial-XXXXXXXX -c baudrate=115200
```

Everything below happens through that monitor.

- [ ] Boot number at startup: `______`
      Type `status` to read it, rather than trying to catch it at boot.
      `upload` resets the board itself the moment flashing finishes —
      before `monitor` has even started — so the sketch's own startup
      line is reliably gone by the time you are watching; you will
      likely see a few lines of ESP32 boot-ROM text (`ets Jun 8 2016
      ...`, `rst:`, `configsip:`) and then nothing. `status` sidesteps
      that entirely and prints the same information on demand:

          boot number      : 3
          this boot's cause: power on

      The number itself counts up on every reset — including one you
      did not ask for, like a brownout — which is the whole reason it is
      worth writing down now: it gives you a baseline to compare against
      after the runs below. See **Brownout** at the end of this step.
- [ ] Hold **the same top-face button as Step 1** until water comes out
      of the outlet — this is priming, and that water does not count.
      There is nothing to press on the watering unit itself; the button
      is always on the controller.
- [ ] Empty the measuring jug

### Flow rate

Run it three times. `run 10`, read the jug, `ml <n>`.

| Run | ml collected | ml/second |
|---|---|---|
| 1 |  |  |
| 2 |  |  |
| 3 |  |  |

    Average flow rate:  ________ ml/s
    Suggested dose:     set pulse ________   (the sketch prints this)

**If the pump does not run at all:** the pin assignment is wrong, or the
Grove cable is not seated. Yellow is `PUMP_EN` on G26 —
[hardware.md](hardware.md).

**If the numbers vary wildly between runs:** normal for a pump this
small, especially the first run after priming. Average them, and lean
towards the *higher* ml/s figure when setting the dose, since that errs
towards less water.

### Brownout

- [ ] Type `status` after the runs. Boot number now: `______`
- [ ] Reset cause reported: `________________________`

    Did it brown out?   yes / no
    Charger used:       ________________

**If the boot number went up and the cause says `BROWNOUT`:** the pump is
starving the board through the Atom's 5V pin. Wire the pump's red 5V wire
directly to the USB supply, ground common — [hardware.md](hardware.md) —
then repeat this step. **Wire both kits the same way**, whichever way it
turns out.

**If it browns out only with one charger:** use the better one, and note
which. Do not ship a kit with a charger that browns out.

---

## Step 3 — `02_read_sensor` (an hour, mostly waiting)

What the sensor actually reads. The pump is never used by this sketch.

Capture each reading with a button click, and write the smoothed value:

| Where the probe is | Smoothed reading |
|---|---|
| Clean and dry, in open air |  |
| Standing in a glass of water |  |
| Pushed into dry soil in the pot |  |
| Same pot, 30 min after watering thoroughly |  |

    Jitter within a window (steady conditions):  ________ counts
    Direction:  wetter reads   higher / lower

- [ ] Any reading fall outside 100–4000? `yes / no`

**Direction does not matter.** The firmware works it out from the
calibration points, so there is nothing to configure and nothing to get
backwards.

**If any real reading lands near 100 or 4000**, those are the bounds the
firmware treats as a broken cable. Move `SENSOR_RAW_MIN` / `SENSOR_RAW_MAX`
in the sketch so real soil cannot trip a false fault.

### The one that decides everything: how wide is the soil range?

Compare the two spans:

    air → water spread:        ________
    dry soil → wet soil spread: ________

If the soil spread is a decent fraction of the air/water spread, calibrate
with air and water as the guide says, and the default thresholds are a
reasonable start.

**If the soil spread is much narrower** — say a fifth of it — then
calibrating against air and water squashes all real soil readings into a
narrow band of the percentage scale, and thresholds of 30% and 55% will
never be crossed. In that case calibrate against the *soil* endpoints
instead: `cal dry` with the probe in the dry pot, `cal wet` in the same
pot right after watering. The scale then spans the range the device
actually lives in, and the thresholds mean something.

    Calibrating against:   air/water   /   dry soil/wet soil

---

## Step 4 — the real firmware

Flash `plant_watering_buddy`. LED should be **yellow** — not calibrated,
pump disabled.

- [ ] `cal dry` — reading captured: `______`
- [ ] `cal wet` — reading captured: `______`
- [ ] `set pulse ______` (from Step 2)
- [ ] `save`
- [ ] `status` looks right, LED now **blue and breathing**

### Supervised first cycle — do not skip

Assemble at the plant per [user-guide.md](user-guide.md), then force one
dose with `water` and watch the whole thing.

- [ ] Water actually reaches the soil
- [ ] It lands **near the probe**, not in the far corner of the pot
- [ ] LED goes green, then cyan
- [ ] The jar is **lower than the pot** (or it will siphon itself empty)
- [ ] Come back after the soak and check the reading moved

    Moisture before dose: ______%     after soak: ______%

**If the reading barely moved:** either the dose is too small, or the
water is not reaching the probe. Fix the tube position before touching the
dose — the firmware will lock out after three unresponsive doses, which is
it working correctly, not a bug.

---

## Step 5 — record it

- [ ] Copy the flow rate, brownout result, and sensor ranges into
      [hardware.md](hardware.md), tagged with which kit
- [ ] Note anything the docs got wrong, especially the assembly steps in
      [user-guide.md](user-guide.md), which were written from M5Stack's
      documentation rather than from the kit in hand
- [ ] Update any threshold or bound in the sketch that this session proved
      wrong, and say why in the commit message

    Settings this kit ended up with:

        set pulse  ______        set soak   ______
        set below  ______        set above  ______
        set maxday ______

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
