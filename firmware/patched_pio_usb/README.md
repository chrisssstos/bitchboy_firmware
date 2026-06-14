# Patched Pico-PIO-USB Library Files

These files contain patches to fix infinite while loop hangs in the Pico-PIO-USB library.

## Problem

The original library has several infinite `while` loops that can hang forever if interrupts disrupt PIO timing. This causes Core 1 to freeze, requiring a device unplug/replug to recover.

See: https://github.com/sekigon-gonnoc/Pico-PIO-USB/issues/192

## Solution

Replaced infinite `while (condition) { continue; }` loops with bounded `for` loops that exit after ~50,000 iterations. This prevents permanent hangs while still allowing normal USB timing.

## Patched Files

Copy these files to your Arduino library folder:
```
~/Documents/Arduino/libraries/Pico_PIO_USB/src/
```

### pio_usb.c
- `send_pre()` - 2 while loops patched
- `pio_usb_bus_usb_transfer()` - 3 while loops patched

### pio_usb_ll.h
- `pio_usb_bus_start_receive()` - 1 while loop patched

### pio_usb_host.c
- `pio_usb_host_stop()` - 1 while loop patched
- `pio_usb_host_restart()` - 1 while loop patched

## Installation

```bash
cp patched_pio_usb/*.c patched_pio_usb/*.h ~/Documents/Arduino/libraries/Pico_PIO_USB/src/
```
