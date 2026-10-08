# Flashing the BitchBoy

End users never install the Arduino IDE, libraries, or touch source code.
One universal firmware works on every unit because per-device slider/pot
calibration lives in the device's own flash (EEPROM emulation) and is set
with an on-device calibration mode — it survives firmware updates.

## For users: web flasher

The web flasher lives on the BitchBoy site: **https://bitchboy.lol/flasher**
(source in the `bitchboy-site-saxion` repo). It pulls the latest firmware from
this repo automatically. This repo only hosts the firmware binary it serves.

1. Open **https://bitchboy.lol/flasher** in **Chrome or Edge** on desktop.
2. Plug in the BitchBoy.
3. **Step 1 — Enter update mode**: click, pick *BitchBoy* from the list.
   The device reboots into its bootloader (LEDs go dark).
4. **Step 2 — Flash**: click, pick *RP2350 Boot* from the list. A few seconds
   later the device restarts with the new firmware. Calibration and settings
   are untouched.

Manual fallback (any browser/OS): hold the BOOT button while plugging in,
then drag `bitchboy-latest.uf2` onto the `RP2350` USB drive that appears.
This preserves calibration too: on current builds (arduino-pico core ≥ 5.x)
the EEPROM/calibration block lives in the second-to-last flash sector, while
picotool's RP2350-E10 workaround block lands on the *last* sector. They no
longer share a sector, so the drag-and-drop write never erases calibration.
The web flasher is still the recommended path (one click, no BOOT button),
but the manual path is now safe as well.

## For users: calibrating sliders/pots (once, or if it ever drifts)

No computer needed:

1. **Hold down the entire bottom row of pads** with one hand and keep it
   held. With the other hand, on the 3×3 grid (bottom-left) press the four
   **corners clockwise twice**: top-left, top-right, bottom-right,
   bottom-left — and again. Releasing the bottom row mid-gesture cancels it.
   (This two-handed hold is a safety interlock so calibration can't be
   entered by accident.)
2. **Key test:** every pad lights dim red. Press each pad once — it turns
   green (white while held). A pad that stays red has a bad switch; one
   that never lights at all has a bad LED; one that stays white is stuck.
   When all pads are green the keypad flashes green and moves on by itself.
   If a pad is dead, **hold B** (top-right grid corner) for 1.5 s to skip
   ahead (amber flash).
3. The keypad switches to the slider/pot display: pixels 0–11 = sliders,
   row 3 = pots. Red = not calibrated yet.
4. Sweep **every slider and pot through its full travel** (end to end).
   Each channel's LED turns green once it has seen enough range.
5. Press the grid's bottom-right corner (**A**, lit green) to save, or
   top-right (**B**, lit white) to cancel.

Only channels that were fully swept get updated, so you can recalibrate a
single slider without touching the rest. Values are stored in the last
flash sector and survive reflashes via the web flasher.

## Windows 11: LEDs / MIDI feedback not working

Windows 11's MIDI 2.0 update (Windows MIDI Services) leaves USB MIDI 1.0
devices on the old "USB Audio Device" driver. On that driver MIDI *from* the
BitchBoy works but MIDI *to* it doesn't, so LEDs don't light up from
Resolume/Ableton ([Resolume's article](https://resolume.com/support/en/midi-troubles-on-windows-with-midi-2-0-update)).
The fix is to switch the device to the new "USBMidi2-ACX" driver:

1. Close Resolume, Ableton and other MIDI software, and plug in the BitchBoy.
2. Download [`windows/BitchBoy-Windows-MIDI-Fix.bat`](windows/BitchBoy-Windows-MIDI-Fix.bat)
   and double-click it. Allow the administrator prompt.
3. When it says *Done*, unplug and replug the BitchBoy.

It only changes BitchBoys that are still on the old driver, and does nothing
on PCs without the MIDI 2.0 update. Windows remembers the choice per unit, so
it's needed once per BitchBoy per PC; firmware updates keep it. To undo, run
it from a command prompt as `BitchBoy-Windows-MIDI-Fix.bat undo`.

If Windows warns about the file ("Windows protected your PC"), click
*More info* → *Run anyway*; it's a plain text script you can open and read.
Manual alternative: Device Manager → *Sound, video and game controllers* →
right-click *BitchBoy* → *Update driver* → *Browse my computer* → *Let me pick*
→ **USBMidi2-ACX**, then replug.

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
- applies `firmware/patched_pio_usb/` over the Pico PIO USB
  library (bounded busy-loop fix)
- deliberately does **not** install the "Adafruit TinyUSB Library" package —
  the copy bundled with the core must be used

Outputs land in `dist/` (versioned) and `flasher/firmware/bitchboy-latest.uf2`
(what the web page serves). You normally don't run this by hand to release:
CI (`.github/workflows/build.yml`) rebuilds the UF2 and commits the refreshed
`flasher/firmware/bitchboy-latest.uf2` back to `main` whenever the firmware
source changes, so the hosted flasher stays current automatically.

The flasher UI itself is not hosted from this repo — it lives on the BitchBoy
site (`bitchboy-site-saxion`, deployed to https://bitchboy.lol/flasher) and
fetches the firmware binary from this repo over `raw.githubusercontent.com`.
This repo only needs to keep that binary current on `main` (the CI above does
that) and be **public** (a private repo 404s the cross-origin fetch).

## How the web flasher works

- **Step 1** opens the BitchBoy's CDC serial port at **1200 baud** and closes
  it — Adafruit TinyUSB treats that as "reboot into the ROM bootloader"
  (same mechanism the Arduino IDE uses to upload).
- **Step 2** talks **PICOBOOT** (the RP2040/RP2350 ROM bootloader's native
  USB protocol) over WebUSB: exclusive access → exit XIP → per-4KB-sector
  erase + write → reboot. The UF2 is parsed in the browser and the E10
  ABSOLUTE block is skipped. (With current builds calibration would survive
  even without skipping it — it lives in the second-to-last sector while the
  E10 block targets the last sector — but skipping it avoids touching that
  top sector at all, and saves a redundant erase/write.)
