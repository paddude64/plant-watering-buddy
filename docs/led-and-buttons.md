# What the light means, and what the button does

The Atom Lite has one LED and one button, so both have to work hard.
The rule: **colour says what state it is in, movement says it is alive.**
An idle device and a crashed device must never look the same, which is why
"everything is fine" is a slow breathing pulse rather than a steady glow.

## The light

| Colour | Movement | Meaning | Do you need to do anything? |
|---|---|---|---|
| **Blue** | slow breathing | Idle. Watching the soil, everything fine. | No |
| **Green** | steady | Watering right now. | No |
| **Cyan** | slow breathing | Just watered; waiting for the water to soak down before measuring again. | No |
| **White** | steady | You are holding the button, so the pump is running. | Let go |
| **Yellow** | steady | Not calibrated. The pump is disabled until it is. | Yes — see below |
| **Magenta** | fast blink | The sensor reading makes no sense. Usually the Grove cable. | Yes — check the cable |
| **Red** | slow blink | Safety lockout. It has stopped watering on purpose. | Yes — see below |

Cyan is the normal state for most of the day after a watering. It is not a
problem; it means the design is working, because the device refuses to trust
the sensor while water is still working its way down to the probe.

### Red: what to check

The device locks out when it waters repeatedly and the soil never gets any
wetter. Almost always one of:

1. **The water jar is empty.** Most likely by far.
2. **The tube is kinked**, or its end has floated up above the water.
3. **The probe has been pushed out of the soil**, or is in dry soil far away
   from where the water actually lands.

Fix the cause, then double-click the button to clear it.

Note that the serial `stop` command will **not** clear a lockout — it only
turns the pump off. Clearing is `clear`, or a double-click, and it is
separate on purpose: a device that stopped because water was not reaching
the soil should not quietly start watering again just because someone asked
the pump to stop.

There is a second, milder red: it has used up the maximum number of doses
allowed in one day. That one clears itself, so you do not need to do
anything.

### Yellow: calibration

A brand-new device does not know what "dry" and "wet" read like on your
particular sensor in your particular soil, and guessing would mean guessing
how much water to add. So it refuses to run the pump until told. This is a
once-per-device job — it is stored in flash and survives reflashing.

Connect over USB and see [mac-setup.md](mac-setup.md) for the serial
monitor, then:

```
cal dry     with the probe clean and dry, held in open air
cal wet     with the probe standing in a glass of water
save
```

Type `status` at any point to see everything the device currently thinks.

## The button

The whole top face of the cube is the button. (The small button on the side
is reset.)

| Action | What happens |
|---|---|
| **Single click** | Prints a full status report to the serial monitor. |
| **Double click** | Clears a lockout, and resets the day's dose count. |
| **Hold** | Runs the pump for as long as you hold it, up to 15 seconds. |

Hold works even during a lockout — if you are standing there with your
finger on the button, you can see what is happening better than the sensor
can. It is the right way to prime the tube when first setting up.
