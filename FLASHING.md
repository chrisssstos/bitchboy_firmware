# Flashing the BitchBoy

End users never install the Arduino IDE, libraries, or touch source code.
One universal firmware works on every unit because per-device slider/pot
calibration lives in the device's own flash (EEPROM emulation) and is set
with an on-device calibration mode — it survives firmware updates.

## For users: web flasher

1. Open the flasher page (`flasher/index.html`, hosted e.g. on GitHub Pages)
   in **Chrome or Edge** on desktop.
2. Plug in the BitchBoy.
3. **Step 1 — Enter update mode**: click, pick *BitchBoy* from the list.
   The device reboots into its bootloader (LEDs go dark).
4. **Step 2 — Flash**: click, pick *RP2350 Boot* from the list. A few seconds
   later the device restarts with the new firmware. Calibration and settings
   are untouched.

Manual fallback (any browser/OS): hold the BOOT button while plugging in,
then drag `bitchboy-latest.uf2` onto the `RP2350` USB drive that appears.
Note: the drag-and-drop path writes picotool's RP2350-E10 workaround block,
which on 4 MB flash parts lands on the calibration sector — so calibration
may need to be redone afterwards. The web flasher skips that block and
preserves calibration; it is the recommended path.

## For users: calibrating sliders/pots (once, or after the fallback path)

No computer needed:

1. **Hold down the entire bottom row of pads** with one hand and keep it
   held. With the other hand, on the 3×3 grid (bottom-left) press the four
   **corners clockwise twice**: top-left, top-right, bottom-right,
   bottom-left — and again. Releasing the bottom row mid-gesture cancels it.
   (This two-handed hold is a safety interlock so calibration can't be
   entered by accident.)
2. The keypad switches to calibration display: pixels 0–11 = sliders,
   row 3 = pots. Red = not calibrated yet.
3. Sweep **every slider and pot through its full travel** (end to end).
   Each channel's LED turns green once it has seen enough range.
4. Press the grid's bottom-right corner (**A**, lit green) to save, or
   top-right (**B**, lit white) to cancel.

Only channels that were fully swept get updated, so you can recalibrate a
single slider without touching the rest. Values are stored in the last
flash sector and survive reflashes via the web flasher.

## For maintainers: building a release

```bash
brew install arduino-cli   # once
./tools/build_firmware.sh
```

The script reproduces the known-good IDE setup exactly (see pins inside):

- Board **Raspberry Pi Pico 2** (`rp2040:rp2040:rpipico2`), ARM, **120 MHz**
  (Pico-PIO-USB needs a multiple of 12 MHz), USB stack **Adafruit TinyUSB**
- arduino-pico core and all libraries at pinned versions, installed into
  `build/arduino-user/` so your own sketchbook is never touched
- applies `bitchboy_code_claude/patched_pio_usb/` over the Pico PIO USB
  library (bounded busy-loop fix)
- deliberately does **not** install the "Adafruit TinyUSB Library" package —
  the copy bundled with the core must be used

Outputs land in `dist/` (versioned) and `flasher/firmware/bitchboy-latest.uf2`
(what the web page serves). To release: build, commit the updated
`flasher/firmware/bitchboy-latest.uf2`, push — GitHub Pages serves the rest.

To host the flasher on GitHub Pages: repo Settings → Pages → deploy from
branch, folder `/flasher` (or copy `flasher/` to a `gh-pages` branch /
`docs/` folder). The page is fully static.

## How the web flasher works

- **Step 1** opens the BitchBoy's CDC serial port at **1200 baud** and closes
  it — Adafruit TinyUSB treats that as "reboot into the ROM bootloader"
  (same mechanism the Arduino IDE uses to upload).
- **Step 2** talks **PICOBOOT** (the RP2040/RP2350 ROM bootloader's native
  USB protocol) over WebUSB: exclusive access → exit XIP → per-4KB-sector
  erase + write → reboot. The UF2 is parsed in the browser; the E10
  ABSOLUTE block is skipped (see above), and the EEPROM/calibration sector
  at the end of flash is never written.
