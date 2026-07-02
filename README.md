# BitchBoy Firmware

Firmware and tooling for the **BitchBoy** built on an
RP2350 (Raspberry Pi Pico 2).

## Just want to flash / update firmware? (no coding)

Open the web flasher in **Chrome or Edge** and follow the two steps:

**→ https://bitchboy.lol/flasher**

It reboots the device into its bootloader and writes the latest firmware from
your browser — no Arduino, no drivers, no install. Your slider/pot calibration
and settings are preserved. Full walkthrough (incl. on-device calibration) in
[FLASHING.md](FLASHING.md).

> The flasher UI lives in the site repo (`bitchboy-site-saxion`); it fetches the
> firmware binary from **this** repo. This repo hosts the firmware + build.

> Tip: if "Enter update mode" can't open the device, quit any app that might be
> using it (Ableton or another DAW, MIDI tools, other browser tabs), replug,
> and try again.

## Want to modify the firmware? (developers)

```bash
# prerequisites (macOS shown; Linux: install arduino-cli + bash)
brew install git arduino-cli

git clone https://github.com/chrisssstos/bitchboy_firmware.git
cd bitchboy_firmware

# edit the firmware
#   firmware/bitchboy_main.ino

# build — installs a pinned, isolated toolchain, applies the pio_usb patch,
# and outputs the UF2 (does not touch your own Arduino setup)
./tools/build_firmware.sh
```

Output:
- `flasher/firmware/bitchboy-latest.uf2` — the binary the site flasher fetches
- `dist/bitchboy-<git-rev>.uf2` — a versioned copy

Flash the result the same way users do: pick it with **"use local .uf2"** in
the site flasher, or drag it onto the `RP2350` USB drive.

CI (`.github/workflows/build.yml`) rebuilds the UF2 from source whenever the
firmware changes and commits the refreshed `bitchboy-latest.uf2` back to `main`,
so the binary the site fetches is never stale — no manual rebuild needed.

## Repo layout

| Path | What |
|---|---|
| `firmware/bitchboy_main.ino` | The firmware |
| `firmware/patched_pio_usb/` | Required Pico-PIO-USB patch (bounded busy-loops) |
| `ableton/BitchBoy/` | Ableton Live control-surface script |
| `flasher/firmware/` | Built firmware binary the site flasher fetches |
| `tools/build_firmware.sh` | One-command reproducible build |
| `hardware/` | Enclosure CAD |
| `experiments/` | Old/experimental sketches — not built, kept for reference |
| `FLASHING.md` | End-user flashing + calibration guide |

## Notes for builders

The build pins the arduino-pico core and library versions, builds for
**Raspberry Pi Pico 2 / RP2350 @ 120 MHz** with the **Adafruit TinyUSB** stack,
and applies `patched_pio_usb/` over the installed Pico PIO USB library. It
deliberately does **not** install the separate "Adafruit TinyUSB Library"
package — the copy bundled with the core must be used. See the header of
`tools/build_firmware.sh` for the exact pinned versions.

---

The build tooling, CI, and documentation in this repo (and the web flasher in
the site repo) were generated with [Claude](https://claude.com/claude-code) (Anthropic).
