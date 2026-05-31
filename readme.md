# Force Off

A standalone [WLED](https://github.com/wled/WLED) usermod that keeps addressable LED strips reliably dark while WLED is **off**.

While off, it continuously re-sends the off (black) frame to the strip, so LEDs that randomly flicker on from electrical noise are driven back off within one frame.

## The problem

On some setups — especially WS2812-type strips driven by an ESP8266 — individual LEDs (often the first one or two on the strip) randomly light up while WLED is supposedly turned **off**. They stay lit until you toggle the light on and off again.

## Root cause

After the light has been off for about 600 ms, `handleOnOff()` (in WLED's `button.cpp`) sets `offMode = true` and calls `BusManager::off()` to save power.

The WLED main loop only services the strip when:

```cpp
if (!offMode || strip.isOffRefreshRequired() || strip.needsUpdate())
  strip.service();
```

For ordinary clocked addressable buses — WS2812, SK6812, WS2811, and friends — `isOffRefreshRequired()` is `false`. So once `offMode` is set, **no data is sent to the strip at all** and the data line sits idle.

With the data line idle (long wiring runs, noisy environments), EMI can latch a spurious value into the first LED(s) and turn them on. Because WLED is no longer transmitting anything, nothing ever corrects them.

## How the usermod works

While the global brightness is `0` (light off) and the usermod is enabled, it keeps WLED out of the power-save `offMode`:

- If WLED had already parked the bus, it calls `BusManager::on()` to bring the bus back so the data line is driven again (mirroring what `handleOnOff()` does on turn-on — re-initing the bus pin, driving the relay, etc.).
- It keeps `lastOnTime` fresh every loop, so the 600 ms off-timeout never fires.

As a result the main loop keeps calling `strip.service()` → `show()`, pushing the black off frame every frame at the configured LED refresh rate. Any LED that glitches on is immediately overwritten with black on the next frame.

When the light is on, the usermod does nothing and leaves everything to stock WLED.

## Trade-offs

Keeping the strip actively driven while off means:

- **ESP8266 modem sleep is disabled while off**, so idle power draw is slightly higher.
- A configured power **relay is kept on** while off.

Both are required to keep the data line driven. If you'd rather have the stock power-saving behaviour, **disable the usermod** — that restores WLED's normal off behaviour completely.

## Settings

The usermod is **enabled by default** once compiled in. You can toggle it under **Settings → Usermods → Force Off**.

## Building it into WLED

This usermod is referenced from a WLED checkout via `custom_usermods` in `platformio_override.ini`.

Add it to the env you build, either by Git URL or as a local `file://` clone.

**By URL** (no local clone needed):

```ini
[env:nodemcuv2]
extends = env:nodemcuv2
custom_usermods =
  ${env:nodemcuv2.custom_usermods}
  https://github.com/daveyijzermans/wled-usermod-force-off.git#main
```

**From a local clone** — clone it alongside your WLED checkout and point at the folder:

```ini
[env:nodemcuv2]
extends = env:nodemcuv2
custom_usermods =
  ${env:nodemcuv2.custom_usermods}
  file:///home/you/projects/wled-usermod-force-off
```

Then build the target env and flash:

```sh
pio run -e nodemcuv2
```

(Use whichever env matches your board — `nodemcuv2` is a common ESP8266 target.)

## What's in this repo

**`force_off.cpp`** — the usermod itself. A single `ForceOffUsermod` class that does the work described above; it self-registers via `REGISTER_USERMOD(...)`, so there is no `usermods_list.cpp` to edit.

**`library.json`** — PlatformIO library manifest. The `"libArchive": false` setting is required; without it the build will fail.
