# Plant Watering Buddy — building it and living with it

An automatic waterer for one plant. It checks the soil and gives it a
small drink when it needs one.

This guide has two parts. **Part 1** is the one-time build: flashing,
calibrating and assembling. **Part 2** is everyday use, and it is short,
because there is almost nothing to do.

> **A note on Part 1:** the assembly steps are written from M5Stack's
> documentation, not from having the kit in hand. Whoever unpacks first
> should correct anything that does not match reality and push the fix.

---

# Part 1 — Building it (once)

Set aside an evening. Most of it is waiting for a download.

## 1. What you have

- **The controller** — a small cube. This is the computer.
- **The watering unit** — a wand that goes in the soil, with a moisture
  sensor and a small pump, plus tubing.
- **A 4-pin Grove cable** joining the two.
- **A USB-C cable**, and any normal phone charger.

You also need, from around the house: a jar for water, a measuring jug,
and a sink to work over.

## 2. Get the toolchain working — before touching the watering unit

Follow **[mac-setup.md](mac-setup.md)** and flash `01_blink`. It takes
about fifteen minutes, nearly all of it a one-off toolchain download —
the first build genuinely does sit there for several minutes, so do not
assume it has hung.

Do not connect the watering unit yet. Get the LED changing colour first.
If something is wrong, you want to be debugging one thing, not three.

**Do not skip to the last step and flash the real firmware.** It will
refuse to run the pump anyway until it has been calibrated, and the two
sketches below are how you get the numbers to calibrate it with.

## 3. Look at the sensor — `03_read_sensor`

Plug the watering unit into the controller's Grove port. This sketch
never runs the pump, so nothing can go wrong or get wet.

Flash it, open the serial monitor, and try the exercise in the top of the
file: the probe dry in air, then in a glass of water, then in the actual
pot's soil dry, then the same pot watered.

Two things to come away with: **which direction the numbers go** when it
gets wetter, and **how far apart dry soil and wet soil really are**. Write
them in [hardware.md](hardware.md).

## 4. Measure the pump — `02_pump_pulse`

**Do this over a sink, with the outlet tube in a measuring jug. Not over
a plant.**

The firmware doses in seconds of pumping, so it needs to know how much
water a second of pumping actually is. M5Stack do not publish a figure,
and two pumps will not match, so each kit gets measured.

1. Inlet tube in a jug of water, outlet tube into an empty measuring jug.
2. Hold the button for a few seconds to prime — the tube has to fill
   before anything comes out the far end, and that water must not count.
3. Empty the measuring jug.
4. Type `run 10`.
5. Read the jug, type `ml 45` (or whatever it really was).

It prints your flow rate and the exact `set pulse` value to use. Do it
two or three times and average.

**While you are here, watch for brownouts.** The pump can draw more
current than the controller is officially rated to pass through. Note the
boot number the sketch prints at startup, run the pump a few times, and
type `status`. If the boot number has gone up and the cause says
`BROWNOUT`, the pump is starving the board — [hardware.md](hardware.md)
has the wiring fix. Test with the charger you actually intend to leave it
plugged into; a weak one and a good one behave very differently.

Record the result in [hardware.md](hardware.md).

## 5. Flash the real firmware and calibrate it

Flash `plant_watering_buddy`. The light will be **yellow**: it is not
calibrated, and it will not run the pump until it is. That is deliberate —
an uncalibrated dose is a guessed dose.

In the serial monitor, type `help` to see everything it can do. Then:

```
cal dry            probe clean and dry, held in open air
cal wet            probe standing in a glass of water
set pulse 6        the number sketch 02 gave you
save
```

Type `status` to check it. The light should go **blue and breathing**.

This is stored in flash, so it survives reflashing. You will not have to
do it again unless you move the probe to a very different pot.

## 6. Assemble it at the plant

1. Push the wand into the soil, close enough to the plant to matter but
   not hard against the roots. Push it to the same depth every time you
   move it — the calibration assumes a depth. A piece of tape on the wand
   makes a useful depth mark.
2. Stand a jar of water next to the pot.
3. Put the inlet tube into the jar, with its end properly under the water,
   not resting above it.
4. Run the outlet tube to the pot, so it delivers water to the soil
   surface near the wand — not in the far corner of the pot, or the water
   will never reach the sensor and the device will conclude something is
   broken.
5. Plug in the USB-C cable and the charger.

**Keep the water jar lower than the pot.** If the jar sits higher, water
can siphon through the tube on its own once it is primed, with the pump
off, and keep going until the jar is empty. The firmware cannot see this
happening and cannot stop it. Jar on the floor, pot on the windowsill.

Keep the controller cube itself away from splashes. Only the wand is meant
to get wet.

## 7. Watch the first cycle before you trust it

Do not walk away yet. With the serial monitor still connected, type
`water` to force one dose.

Watch that: water actually arrives at the soil, it arrives near the wand,
and the light goes **green** then **cyan**. Cyan means it has stopped and
is waiting for the water to soak down before measuring again — that wait
is the whole point of the design, and it is normal for it to sit there for
twenty minutes.

Then leave it alone and check on it the next day.

---

# Part 2 — Living with it

Day to day there is nothing to do except keep the jar full.

## Every few days

- Look at the water jar. Top it up before it runs low.
- Look at the light. Blue or cyan means everything is fine.

That is all.

## What the light means

| Light | Meaning | Do something? |
|---|---|---|
| **Blue**, slowly breathing | Watching the soil. All fine. | No |
| **Cyan**, slowly breathing | Just watered. Waiting for it to soak in. | No |
| **Green** | Watering right now. | No |
| **Yellow** | Not set up yet. | Yes — Part 1, step 5 |
| **Red**, slow even blink | It has stopped on purpose. | Yes — see below |
| **Red**, flashing in groups | Something is wrong with the device itself. | Yes — count the flashes, see [led-and-buttons.md](led-and-buttons.md) |
| **Magenta**, blinking fast | It cannot read the sensor. | Check the Grove cable |

Full details in [led-and-buttons.md](led-and-buttons.md).

## If the light is red

It waters, then checks whether the soil got any wetter. If it tries three
times and nothing changes — about an hour, since it waits for each dose to
soak in — it stops and waits for you, rather than carrying on pumping at
an empty jar. Usually one of these:

1. **The water jar is empty.** This is nearly always the answer.
2. **The tube is kinked**, or its end is no longer under the water.
3. **The wand has come out of the soil**, or the water is landing too far
   from it.

Fix it, then double-click the button to start it again.

## Things worth knowing

- Do not let the jar sit empty for long. Running the little pump dry is
  not good for it.
- Keep the controller away from water. Only the wand goes in the soil.
- Any normal USB-C phone charger will run it.
- If you move the wand to a different pot, or push it to a different
  depth, calibrate it again (Part 1, step 5).
- If you unplug it for a while, it keeps counting the day's waterings
  from where it left off. It has no clock, so it cannot tell how long it
  was off, and it would rather water too little than too much. Double-
  click the button to reset that.

## Talking to it over serial

Connect at 115200 baud — see [mac-setup.md](mac-setup.md) — and type `help`
for the list. Nothing here is needed day to day; it is for setting up,
adjusting, and finding out what the device thinks.

| Command | What it does |
|---|---|
| `status` | Everything it currently thinks: state, reading, doses used, settings |
| `read` | One moisture reading |
| `cal dry` / `cal wet` | Set the calibration references |
| `cal clear` | Forget calibration; the pump is disabled until it is redone |
| `set pulse <sec>` | How long one dose runs the pump |
| `set soak <min>` | How long to ignore the sensor after a dose |
| `set below <pct>` | Start watering below this moisture |
| `set above <pct>` | Stop watering above this moisture |
| `set maxday <n>` | Doses allowed per day |
| `water` | Force one dose now |
| `stop` | Turn the pump off |
| `clear` | Clear a lockout and reset the day's dose count |
| `save` | Write settings to flash |

Settings changed with `set` are lost on reboot unless you `save` them.

### `stop` and `clear` are not the same thing

**`stop` turns the pump off. It does not clear a lockout**, and that is
deliberate. If the device has stopped because water never reached the soil,
"stop" resuming normal watering would be exactly the wrong response — it
would go straight back to pumping into an empty jar.

So: `stop` when you just want the pump off. `clear` only once you have
actually fixed whatever caused the red light. `clear` also resets the
count of doses used today, which is the same thing a double-click of the
button does.

## If something seems wrong

**The light is off completely.** Check both ends of the USB-C cable, and
try a different cable — a surprising number are charge-only.

**The soil is dry and nothing is happening.** Connect the serial monitor
and type `status`. It will say what it thinks and why. If the light is
yellow it was never calibrated.

**The pump runs but no water arrives.** The tube needs priming. Hold the
button down for a few seconds to run the pump continuously until water
comes through.

**The plant is getting too much or too little water.** Adjust it:
`set below 25` waters at a drier point, `set above 60` waters for longer
before stopping, `set pulse 4` changes the size of each dose. Then `save`.
Type `status` to see the current settings.

## Putting it away

If it is going into a cupboard for the winter, or you are moving and it
will be boxed up for a while, get the water out of it first. Two minutes
now saves finding a slimy tube and a scaled-up pump later.

Water left standing in the tube grows biofilm. In a hard-water area the
pump head furs up with limescale. And if wherever you are storing it can
freeze, draining stops being good practice and becomes necessary — water
expanding inside a pump head is how they crack.

### Draining it

1. **Lift the inlet tube out of the water.** Hold the outlet end over a
   sink.
2. **Hold the button for a few seconds.** The pump pushes the water in
   the tube out, then starts pulling air through behind it.
3. **Do that once or twice more**, until nothing else comes out.
4. **Unplug it, disconnect the tubes**, and shake the water out of them.
5. **Leave it all out to dry** before it goes in a box. A day is plenty.

### One thing to be careful about

Everywhere else this guide tells you not to run the pump dry, and that is
still true. The difference is how long. **A few seconds of running dry to
clear the tube is fine and is how you empty one of these.** What harms a
small pump is running dry for a sustained stretch, because the water it
is moving is also what keeps it cool.

So: short presses, not one long one.

### When it comes back out

Nothing to redo. The calibration and settings live in the controller's
flash and survive being unplugged for as long as you like — plug it back
in, prime the tube again, and it picks up where it left off.

Worth checking on the way out: if the pump still seems to hold water
after a couple of purge runs, that is worth knowing about this hardware.
Make a note of it.
