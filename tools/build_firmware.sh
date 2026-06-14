#!/usr/bin/env bash
# Build the universal BitchBoy firmware UF2 with one command.
#
#   ./tools/build_firmware.sh
#
# Reproduces the known-good Arduino IDE configuration exactly:
#   - Board:     Raspberry Pi Pico 2 (RP2350, ARM), CPU 120 MHz (required by
#                Pico-PIO-USB: sysclock must be a multiple of 12 MHz)
#   - USB stack: Adafruit TinyUSB
#   - Core:      rp2040:rp2040 (earlephilhower arduino-pico), pinned below
#   - Libraries: pinned below, installed into an isolated directory so the
#                machine's own sketchbook is never touched
#   - Patch:     firmware/patched_pio_usb/ files are copied over
#                the installed Pico PIO USB library (bounded busy-loops fix,
#                see that folder's README)
#
# Requires arduino-cli (brew install arduino-cli). First run downloads the
# core + libraries; later runs are offline-friendly.
#
# Output:
#   dist/bitchboy-<git-rev>.uf2
#   flasher/firmware/bitchboy-latest.uf2   (what the web flasher serves)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_INO="$REPO_ROOT/firmware/bitchboy_main.ino"
PATCH_DIR="$REPO_ROOT/firmware/patched_pio_usb"
BUILD_DIR="$REPO_ROOT/build"
DIST_DIR="$REPO_ROOT/dist"
FLASHER_FW_DIR="$REPO_ROOT/flasher/firmware"

PICO_INDEX_URL="https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json"
CORE="rp2040:rp2040"
CORE_VERSION="5.4.3"
FQBN="rp2040:rp2040:rpipico2:arch=arm,freq=120,usbstack=tinyusb"

# Library versions known to work with this firmware (Adafruit TinyUSB itself
# is bundled with the core and selected via usbstack=tinyusb).
LIBS=(
  "Adafruit TCA8418@1.0.2"
  "Adafruit BusIO@1.17.4"
  "Adafruit NeoPixel@1.15.2"
  "CD74HC4067@1.0.2"
  "MIDI Library@5.0.2"
  "Pico PIO USB@0.7.2"
)

command -v arduino-cli >/dev/null || {
  echo "arduino-cli not found. Install it with: brew install arduino-cli" >&2
  exit 1
}

# Isolate libraries (and only libraries) from the user's sketchbook. The data
# directory is left at its default so an already-installed core is reused.
export ARDUINO_DIRECTORIES_USER="$BUILD_DIR/arduino-user"
mkdir -p "$ARDUINO_DIRECTORIES_USER"

echo "==> Installing core $CORE@$CORE_VERSION (no-op if present)"
arduino-cli core update-index --additional-urls "$PICO_INDEX_URL"
arduino-cli core install "$CORE@$CORE_VERSION" --additional-urls "$PICO_INDEX_URL"

echo "==> Installing pinned libraries"
arduino-cli lib update-index
for lib in "${LIBS[@]}"; do
  # --no-deps: dependency resolution would pull in "Adafruit TinyUSB Library",
  # which must NOT be installed separately — it would shadow the copy bundled
  # with the arduino-pico core and break the build at runtime.
  arduino-cli lib install --no-deps "$lib"
done
rm -rf "$ARDUINO_DIRECTORIES_USER/libraries/Adafruit_TinyUSB_Library"

echo "==> Applying Pico-PIO-USB patch (bounded busy-loops)"
PIO_USB_SRC="$ARDUINO_DIRECTORIES_USER/libraries/Pico_PIO_USB/src"
[ -d "$PIO_USB_SRC" ] || { echo "Pico_PIO_USB library not found at $PIO_USB_SRC" >&2; exit 1; }
cp "$PATCH_DIR"/*.c "$PATCH_DIR"/*.h "$PIO_USB_SRC/"

# arduino-cli requires sketch dir name == ino name; stage a copy.
echo "==> Staging sketch"
SKETCH_DIR="$BUILD_DIR/sketch/bitchboy"
rm -rf "$SKETCH_DIR"
mkdir -p "$SKETCH_DIR"
cp "$SRC_INO" "$SKETCH_DIR/bitchboy.ino"

echo "==> Compiling ($FQBN)"
arduino-cli compile \
  --fqbn "$FQBN" \
  --output-dir "$BUILD_DIR/out" \
  --warnings default \
  "$SKETCH_DIR"

REV="$(git -C "$REPO_ROOT" describe --always --dirty 2>/dev/null || echo dev)"
mkdir -p "$DIST_DIR" "$FLASHER_FW_DIR"
cp "$BUILD_DIR/out/bitchboy.ino.uf2" "$DIST_DIR/bitchboy-$REV.uf2"
cp "$BUILD_DIR/out/bitchboy.ino.uf2" "$FLASHER_FW_DIR/bitchboy-latest.uf2"

echo
echo "==> Done"
echo "    $DIST_DIR/bitchboy-$REV.uf2"
echo "    $FLASHER_FW_DIR/bitchboy-latest.uf2"
