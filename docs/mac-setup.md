# First 15 minutes on a Mac

Goal: get `01_blink` running on the Atom Lite. Once the LED changes colour,
your toolchain is proven and everything after this is just code.

You need the Atom Lite and a USB-C cable. The Watering Unit stays in its
packaging for now — don't plug it in until sketch `02`.

## 1. Install the toolchain

```bash
brew install arduino-cli
```

That is the whole install. `arduino-cli` downloads the ESP32 compiler
itself on first build.

If you would rather work in a GUI, install the Arduino IDE 2.x as well —
it opens these sketches directly, because they are plain `.ino` sketch
folders. You will need to add this URL under
*Settings → Additional board manager URLs*:

```
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

then install **esp32 by Espressif Systems** from the Boards Manager and
select the **M5Atom** board. The IDE and `arduino-cli` build the same
files, so mix and match freely. The one thing the IDE will *not* do is
respect the pinned versions in `sketch.yaml` — see [Why the versions are
pinned](#why-the-versions-are-pinned) below.

## 2. Plug in the Atom Lite and find its port

```bash
arduino-cli board list
```

You are looking for a line with a port like `/dev/cu.usbserial-XXXXXXXX`.
Note it down — you need it for every upload.

**If nothing appears:** the Atom Lite's USB-to-serial chip is a CH9102F.
Recent macOS includes a driver for it, but not every version does. Check
first with the cable and a different USB port (a surprising number of
USB-C cables are charge-only and carry no data). If the port still doesn't
show up, install WCH's CH34x/CH9102 macOS driver, reboot, and try again.

## 3. Build and upload

```bash
cd sketches/01_blink
arduino-cli compile --profile atomlite
arduino-cli upload --profile atomlite -p /dev/cu.usbserial-XXXXXXXX
```

The first `compile` downloads about 200MB of ESP32 toolchain and takes a
few minutes. Every build after that takes seconds.

## 4. Watch it work

The LED should cycle red → green → blue. Hold the button (the whole top
face of the cube is the button) and it should go white.

Now open the serial monitor to see what it is thinking:

```bash
arduino-cli monitor -p /dev/cu.usbserial-XXXXXXXX -c baudrate=115200
```

Press `Ctrl-C` to quit the monitor. **The monitor holds the serial port
open, so uploads will fail while it is running** — quit it before you
upload again. This will catch you out at least once.

## Why the versions are pinned

Each sketch has a `sketch.yaml` naming the exact ESP32 core and library
versions it builds against. `arduino-cli compile --profile atomlite` uses
those exact versions regardless of what else is installed on your machine,
which is why the same command produces the same binary on your Mac, on
mine, and in GitHub Actions.

If you compile without `--profile`, or build in the Arduino IDE, you get
whatever versions happen to be installed instead. That is fine for
experimenting, but when something behaves differently on two machines,
this is the first thing to check.

## When it goes wrong

**`upload` fails with a timeout or "Failed to connect".**
Usually the serial monitor is still attached — quit it. Otherwise unplug
the device, plug it back in, and check the port name hasn't changed.

**`upload` connects, reads the chip fine, then fails with `esptool`
printing a Python traceback ending `StopIteration` / "The chip stopped
responding" while probing flash.** Seen on a real Atom Lite + Mac: the
default upload speed is too fast for this board/driver/esptool
combination. Every `sketch.yaml` in this repo already pins
`UploadSpeed=115200` on the FQBN for this reason — if you copy a
`sketch.yaml` from elsewhere or hand-roll one, keep that suffix.

**Compile fails complaining about a missing library.**
You probably left off `--profile atomlite`. With the profile, the required
libraries are fetched automatically.

**The LED does nothing but the upload said it succeeded.**
Press the reset button (the small button on the side of the cube), which
restarts the sketch. Some boards don't restart automatically after upload.
