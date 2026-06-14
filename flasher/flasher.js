// BitchBoy web flasher
//
// Step 1: WebSerial 1200-baud touch — Adafruit TinyUSB CDC interprets an
//         open/close at 1200 baud as "reboot into the ROM bootloader".
// Step 2: WebUSB PICOBOOT — talk to the RP2040/RP2350 ROM bootloader
//         directly and write the UF2, block by block.
//
// The E10-erratum ABSOLUTE block (0x10ffff00) that picotool appends to
// RP2350 UF2s is deliberately skipped: it only exists for mass-storage
// drag-and-drop flashing, and on 4MB-flash devices its address wraps onto
// the LAST flash sector. EEPROM-emulated calibration lives in the
// second-to-last sector (arduino-pico core >= 5.x), so skipping the block
// isn't strictly required to preserve calibration — but it avoids erasing
// the top sector and saves a redundant write, so we skip it anyway.

'use strict';

const BITCHBOY_SERIAL_FILTER = { usbVendorId: 0xCafe, usbProductId: 0x4002 };
const BOOTLOADER_USB_FILTER = { vendorId: 0x2e8a };
const RP2040_BOOT_PID = 0x0003;

const UF2_MAGIC0 = 0x0a324655;
const UF2_MAGIC1 = 0x9e5d5157;
const UF2_FLAG_NOT_MAIN_FLASH = 0x00000001;
const UF2_FLAG_FAMILY_PRESENT = 0x00002000;
const FAMILY_ABSOLUTE = 0xe48bff57;

const FLASH_BASE = 0x10000000;
const FLASH_END = 0x11000000;
const SECTOR_SIZE = 4096;

const PICOBOOT_MAGIC = 0x431fd10b;
const CMD_EXCLUSIVE_ACCESS = 0x01;
const CMD_REBOOT = 0x02;
const CMD_FLASH_ERASE = 0x03;
const CMD_WRITE = 0x05;
const CMD_EXIT_XIP = 0x06;
const CMD_REBOOT2 = 0x0a;

const $ = (id) => document.getElementById(id);
const logEl = $('log');

function log(msg) {
  console.log('[flasher]', msg);
  logEl.textContent += msg + '\n';
  logEl.scrollTop = logEl.scrollHeight;
}

function setStatus(el, text, cls) {
  el.textContent = text;
  el.className = 'status' + (cls ? ' ' + cls : '');
}

// ---------------------------------------------------------------------------
// Firmware selection
// ---------------------------------------------------------------------------

let firmwareBytes = null;
let firmwareName = null;

async function loadBundledFirmware() {
  try {
    const resp = await fetch('firmware/bitchboy-latest.uf2', { cache: 'no-cache' });
    if (!resp.ok) throw new Error('HTTP ' + resp.status);
    firmwareBytes = await resp.arrayBuffer();
    firmwareName = 'bitchboy-latest.uf2';
    const blocks = parseUf2(firmwareBytes);
    $('fw-meta').innerHTML =
      `<b>${firmwareName}</b> — ${(firmwareBytes.byteLength / 1024).toFixed(0)} KB, ` +
      `${blocks.flashBlocks.length} blocks (${blocks.familyName})`;
  } catch (e) {
    $('fw-meta').textContent =
      'Bundled firmware not available (' + e.message + ') — pick a local .uf2 file.';
  }
}

$('fw-file').addEventListener('change', async (ev) => {
  const f = ev.target.files[0];
  if (!f) return;
  firmwareBytes = await f.arrayBuffer();
  firmwareName = f.name;
  try {
    const blocks = parseUf2(firmwareBytes);
    $('fw-meta').innerHTML =
      `<b>${firmwareName}</b> — ${(firmwareBytes.byteLength / 1024).toFixed(0)} KB, ` +
      `${blocks.flashBlocks.length} blocks (${blocks.familyName})`;
  } catch (e) {
    firmwareBytes = null;
    $('fw-meta').textContent = 'Not a valid UF2: ' + e.message;
  }
});

// ---------------------------------------------------------------------------
// UF2 parsing
// ---------------------------------------------------------------------------

function familyName(id) {
  switch (id) {
    case 0xe48bff56: return 'RP2040';
    case 0xe48bff59: return 'RP2350 ARM-S';
    case 0xe48bff5a: return 'RP2350 RISC-V';
    case FAMILY_ABSOLUTE: return 'ABSOLUTE';
    default: return '0x' + id.toString(16);
  }
}

function parseUf2(buf) {
  if (buf.byteLength === 0 || buf.byteLength % 512 !== 0) {
    throw new Error('size is not a multiple of 512');
  }
  const dv = new DataView(buf);
  const flashBlocks = [];
  let skipped = 0;
  let mainFamily = null;

  for (let off = 0; off < buf.byteLength; off += 512) {
    if (dv.getUint32(off, true) !== UF2_MAGIC0 ||
        dv.getUint32(off + 4, true) !== UF2_MAGIC1) {
      throw new Error('bad block magic at offset ' + off);
    }
    const flags = dv.getUint32(off + 8, true);
    const addr = dv.getUint32(off + 12, true);
    const size = dv.getUint32(off + 16, true);
    const family = (flags & UF2_FLAG_FAMILY_PRESENT) ? dv.getUint32(off + 28, true) : 0;

    if (flags & UF2_FLAG_NOT_MAIN_FLASH) { skipped++; continue; }
    // E10 drag-and-drop workaround block — must not be written via PICOBOOT
    // (wraps onto the EEPROM/calibration sector on small flash parts).
    if (family === FAMILY_ABSOLUTE && addr >= 0x10ff0000) { skipped++; continue; }
    if (addr < FLASH_BASE || addr + size > FLASH_END) { skipped++; continue; }
    if (size === 0 || size > 476) throw new Error('bad payload size ' + size);

    flashBlocks.push({ addr, data: new Uint8Array(buf, off + 32, size) });
    if (mainFamily === null) mainFamily = family;
  }

  if (flashBlocks.length === 0) throw new Error('no flashable blocks');
  return { flashBlocks, skipped, family: mainFamily, familyName: familyName(mainFamily) };
}

// Group blocks into 4KB-sector images (erase granularity).
function blocksToSectors(flashBlocks) {
  const sectors = new Map();
  for (const b of flashBlocks) {
    let rem = b.data;
    let addr = b.addr;
    while (rem.length > 0) {
      const base = Math.floor(addr / SECTOR_SIZE) * SECTOR_SIZE;
      if (!sectors.has(base)) sectors.set(base, new Uint8Array(SECTOR_SIZE).fill(0xff));
      const offInSector = addr - base;
      const n = Math.min(rem.length, SECTOR_SIZE - offInSector);
      sectors.get(base).set(rem.subarray(0, n), offInSector);
      rem = rem.subarray(n);
      addr += n;
    }
  }
  return [...sectors.entries()].sort((a, b) => a[0] - b[0]);
}

// ---------------------------------------------------------------------------
// PICOBOOT over WebUSB
// ---------------------------------------------------------------------------

class Picoboot {
  constructor(device) {
    this.device = device;
    this.token = 1;
  }

  async connect() {
    await this.device.open();
    if (!this.device.configuration) await this.device.selectConfiguration(1);

    for (const iface of this.device.configuration.interfaces) {
      const alt = iface.alternates[0];
      if (alt.interfaceClass === 0xff && alt.interfaceSubclass === 0x00) {
        this.ifaceNum = iface.interfaceNumber;
        for (const ep of alt.endpoints) {
          if (ep.type === 'bulk' && ep.direction === 'out') this.epOut = ep.endpointNumber;
          if (ep.type === 'bulk' && ep.direction === 'in') this.epIn = ep.endpointNumber;
        }
      }
    }
    if (this.ifaceNum === undefined || this.epOut === undefined || this.epIn === undefined) {
      throw new Error('PICOBOOT interface not found on this device');
    }
    await this.device.claimInterface(this.ifaceNum);
    await this.interfaceReset();
    log(`PICOBOOT connected (interface ${this.ifaceNum}, PID 0x${this.device.productId.toString(16)})`);
  }

  get isRp2350() { return this.device.productId !== RP2040_BOOT_PID; }

  async interfaceReset() {
    await this.device.controlTransferOut({
      requestType: 'vendor', recipient: 'interface',
      request: 0x41, value: 0, index: this.ifaceNum,
    });
  }

  buildCmd(cmdId, cmdSize, transferLength, argsWriter) {
    const buf = new ArrayBuffer(32);
    const dv = new DataView(buf);
    dv.setUint32(0, PICOBOOT_MAGIC, true);
    dv.setUint32(4, this.token++, true);
    dv.setUint8(8, cmdId);
    dv.setUint8(9, cmdSize);
    dv.setUint32(12, transferLength, true);
    if (argsWriter) argsWriter(dv);
    return buf;
  }

  async cmd(cmdId, cmdSize, transferLength, argsWriter, outData) {
    await this.device.transferOut(this.epOut, this.buildCmd(cmdId, cmdSize, transferLength, argsWriter));
    if (outData) {
      const r = await this.device.transferOut(this.epOut, outData);
      if (r.status !== 'ok') throw new Error('data transfer failed: ' + r.status);
    }
    // Acknowledge: zero-length IN transfer completes OUT/no-data commands.
    const ack = await this.device.transferIn(this.epIn, 64);
    if (ack.status === 'stall') {
      await this.device.clearHalt('in', this.epIn);
      throw new Error('command 0x' + cmdId.toString(16) + ' rejected by bootloader');
    }
  }

  exclusiveAccess(exclusive) {
    return this.cmd(CMD_EXCLUSIVE_ACCESS, 1, 0, (dv) => dv.setUint8(16, exclusive));
  }

  exitXip() {
    return this.cmd(CMD_EXIT_XIP, 0, 0);
  }

  flashErase(addr, size) {
    return this.cmd(CMD_FLASH_ERASE, 8, 0, (dv) => {
      dv.setUint32(16, addr, true);
      dv.setUint32(20, size, true);
    });
  }

  write(addr, data) {
    return this.cmd(CMD_WRITE, 8, data.length, (dv) => {
      dv.setUint32(16, addr, true);
      dv.setUint32(20, data.length, true);
    }, data);
  }

  async reboot() {
    if (this.isRp2350) {
      // REBOOT2, flags 0x0 = normal boot
      await this.cmd(CMD_REBOOT2, 16, 0, (dv) => {
        dv.setUint32(16, 0x0, true);   // flags: normal
        dv.setUint32(20, 500, true);   // delay ms
        dv.setUint32(24, 0, true);
        dv.setUint32(28, 0, true);
      });
    } else {
      await this.cmd(CMD_REBOOT, 12, 0, (dv) => {
        dv.setUint32(16, 0, true);     // pc = 0: normal flash boot
        dv.setUint32(20, 0, true);     // sp
        dv.setUint32(24, 500, true);   // delay ms
      });
    }
  }
}

// ---------------------------------------------------------------------------
// Step 1: reboot the running firmware into the bootloader
// ---------------------------------------------------------------------------

$('btn-reboot').addEventListener('click', async () => {
  const status = $('status1');
  try {
    setStatus(status, 'pick the BitchBoy in the browser dialog…', 'busy');
    // No filter: forks and older firmware may use a different USB VID/PID, and
    // the 1200-baud touch works on ANY TinyUSB CDC port regardless of ID. The
    // device advertises the product name "BitchBoy", so it's easy to spot in
    // the chooser. (BITCHBOY_SERIAL_FILTER is kept for reference only.)
    const port = await navigator.serial.requestPort();
    setStatus(status, 'sending reboot command…', 'busy');
    await port.open({ baudRate: 1200 });
    await new Promise((r) => setTimeout(r, 200));
    try { await port.close(); } catch (_) { /* device vanished mid-close: expected */ }
    log('1200-baud touch sent, device should re-enumerate as bootloader');
    setStatus(status, 'done — device is in update mode (LEDs dark). Continue with step 2.', 'ok');
    $('step1').classList.add('done');
    $('step2').classList.add('active');
  } catch (e) {
    if (e.name === 'NotFoundError') {
      // Empty chooser or user cancelled. The BitchBoy must be running normal
      // firmware that exposes a serial port for this step to find it — if it
      // isn't listed, just hold BOOT while plugging in and skip to step 2.
      setStatus(status, "no serial port picked — if none appeared, hold BOOT while plugging in and skip to step 2.", '');
    } else {
      // "Failed to open serial port" almost always means another program has
      // the BitchBoy open (a DAW like Ableton, a MIDI tool, or another tab).
      log('step 1 error: ' + e.message);
      setStatus(status, "couldn't open the port — another app is using the BitchBoy. Close every app that might be connected to it (Ableton or another DAW, MIDI tools, other browser tabs), unplug and replug the BitchBoy, then try again.", 'err');
    }
  }
});

// ---------------------------------------------------------------------------
// Step 2: flash over PICOBOOT
// ---------------------------------------------------------------------------

$('btn-flash').addEventListener('click', async () => {
  const status = $('status2');
  const progress = $('progress');

  if (!firmwareBytes) {
    setStatus(status, 'no firmware loaded — pick a .uf2 above.', 'err');
    return;
  }

  let pb = null;
  try {
    const { flashBlocks, skipped, familyName: fam } = parseUf2(firmwareBytes);
    const sectors = blocksToSectors(flashBlocks);
    log(`firmware: ${flashBlocks.length} blocks → ${sectors.length} sectors (${fam}), ${skipped} block(s) skipped`);

    setStatus(status, 'pick the boot device in the browser dialog…', 'busy');
    const device = await navigator.usb.requestDevice({ filters: [BOOTLOADER_USB_FILTER] });

    pb = new Picoboot(device);
    await pb.connect();

    const chipFam = pb.isRp2350 ? 'RP2350' : 'RP2040';
    if ((fam.startsWith('RP2350') && !pb.isRp2350) || (fam === 'RP2040' && pb.isRp2350)) {
      throw new Error(`firmware is for ${fam} but the connected chip is ${chipFam}`);
    }

    setStatus(status, 'erasing + writing…', 'busy');
    progress.classList.add('visible');
    progress.value = 0;

    await pb.exclusiveAccess(1);
    await pb.exitXip();

    for (let i = 0; i < sectors.length; i++) {
      const [addr, data] = sectors[i];
      await pb.flashErase(addr, SECTOR_SIZE);
      await pb.write(addr, data);
      progress.value = Math.round(((i + 1) / sectors.length) * 100);
    }

    setStatus(status, 'rebooting device…', 'busy');
    await pb.reboot();
    try { await pb.device.close(); } catch (_) { /* gone after reboot */ }

    log('flash complete');
    setStatus(status, '✓ flashed ' + firmwareName + ' — the BitchBoy is restarting. Done!', 'ok');
    $('step2').classList.add('done');
  } catch (e) {
    if (e.name === 'NotFoundError') {
      setStatus(status, 'no device picked. Is it in update mode (step 1)?', '');
    } else {
      log('step 2 error: ' + e.message);
      setStatus(status, 'failed: ' + e.message + ' — unplug/replug and retry, or use the manual fallback.', 'err');
    }
    if (pb && pb.device.opened) {
      try { await pb.device.close(); } catch (_) { /* best effort */ }
    }
  }
});

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

if (!('serial' in navigator) || !('usb' in navigator)) {
  $('unsupported').style.display = 'block';
  $('btn-reboot').disabled = true;
  $('btn-flash').disabled = true;
} else {
  $('step1').classList.add('active');
}
loadBundledFirmware();
