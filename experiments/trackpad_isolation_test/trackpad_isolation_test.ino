// Two-phase BitchBoy trackpad PCB diagnostic.
//
// PHASE A (first ~6 s): line state probing on GPIO 0 / 1 / 2 using internal
//   pulls. Tells us whether the trackpad is electrically present on the bus
//   *before* PIO USB takes over the pins.
//
// PHASE A2 (one-shot, after passive probe): cross-coupling drive test —
//   drives each data pin LOW in turn and reads the other to detect shorts
//   between D+ / D- / neighbouring GPIOs. Safe with trackpad attached
//   (we never source current against an external pullup).
//
// PHASE B (after Phase A): brings up PIO USB host on the configured pin and
//   reports mount / unmount / HID reports — same as the previous test.
//
// Flash, open Serial Monitor @ 115200, plug the trackpad into the PCB USB-C
// port, watch the trace.
//
// PINOUT (matches main firmware):
//   D+ on GPIO 1, D- on GPIO 0 (PIO_USB_PINOUT_DMDP).
//   Confirmed correct by the trackpad pullup experiment: when the trackpad
//   is plugged in, GPIO 1 is pulled high by the trackpad's 1.5 kΩ device-
//   side D+ pullup. The blocker for enumeration is a rogue external pullup
//   on GPIO 0 (D-) that lives on the BitchBoy PCB itself — it must be
//   physically removed for USB host to work.
//
// Adafruit_TinyUSB MUST be the USB stack (Tools > USB Stack). Without it,
// Serial maps to UART0 on GPIO 0/1 and would actively drive the lines we
// want to measure.

#include <Adafruit_TinyUSB.h>
#include <pio_usb.h>
#include "tusb.h"

// D+ on GPIO 1, D- on GPIO 0 (DMDP layout — matches main firmware).
#define HOST_PIN_DP 1
#define HOST_PIN_DM 0
#define EXTRA_PROBE 2     // sanity: probe the next neighbour too

const unsigned long PHASE_A_MS = 6000;

Adafruit_USBH_Host USBHost;

// ---------- cross-core ----------
volatile bool flagStartPioUsb   = false;   // Core 0 -> Core 1 : "you may init now"
volatile bool flagCore1Ready    = false;
volatile bool flagMounted       = false;
volatile bool flagUnmounted     = false;
volatile uint8_t lastDevAddr    = 0;
volatile uint8_t lastInstance   = 0;
volatile unsigned long core1Heartbeat = 0;

#define Q_SIZE 16
struct ReportSlot {
  uint8_t  data[16];
  uint8_t  len;
  uint8_t  devAddr;
  uint8_t  instance;
};
volatile ReportSlot reportQueue[Q_SIZE];
volatile uint8_t qHead = 0, qTail = 0;

// ---------- Phase A: passive line probe ----------
struct PinSnap { int hiz; int pd; int pu; };

PinSnap probePin(int pin) {
  PinSnap s;
  pinMode(pin, INPUT);
  delay(5);
  s.hiz = digitalRead(pin);

  pinMode(pin, INPUT_PULLDOWN);
  delay(5);
  s.pd = digitalRead(pin);

  pinMode(pin, INPUT_PULLUP);
  delay(5);
  s.pu = digitalRead(pin);

  pinMode(pin, INPUT);   // leave as hi-Z so PIO USB can take over cleanly
  return s;
}

const __FlashStringHelper* interpretPin(const PinSnap& s) {
  // PD=HIGH means an external source stronger than the ~50 kΩ internal pull
  // is sourcing current — i.e. there's a real external pullup.
  // PU=LOW means an external source is sinking — real external pulldown.
  if (s.pd == 1 && s.pu == 1) return F("EXTERNALLY PULLED HIGH (strong external pullup)");
  if (s.pd == 0 && s.pu == 0) return F("EXTERNALLY PULLED LOW  (strong external pulldown)");
  if (s.pd == 0 && s.pu == 1) return F("FLOATING (no external drive)");
  return F("INCONSISTENT (noise / weak drive — re-test)");
}

void probeRound(unsigned long elapsedMs) {
  PinSnap dp    = probePin(HOST_PIN_DP);
  PinSnap dm    = probePin(HOST_PIN_DM);
  PinSnap extra = probePin(EXTRA_PROBE);

  Serial.print(F("[")); Serial.print(elapsedMs / 1000); Serial.println(F("s]"));

  Serial.print(F("  D+ (GPIO ")); Serial.print(HOST_PIN_DP); Serial.print(F("): "));
  Serial.print(F("HiZ=")); Serial.print(dp.hiz);
  Serial.print(F(" PD="));  Serial.print(dp.pd);
  Serial.print(F(" PU="));  Serial.print(dp.pu);
  Serial.print(F("  -> "));  Serial.println(interpretPin(dp));

  Serial.print(F("  D- (GPIO ")); Serial.print(HOST_PIN_DM); Serial.print(F("): "));
  Serial.print(F("HiZ=")); Serial.print(dm.hiz);
  Serial.print(F(" PD="));  Serial.print(dm.pd);
  Serial.print(F(" PU="));  Serial.print(dm.pu);
  Serial.print(F("  -> "));  Serial.println(interpretPin(dm));

  Serial.print(F("  ?? (GPIO ")); Serial.print(EXTRA_PROBE); Serial.print(F("): "));
  Serial.print(F("HiZ=")); Serial.print(extra.hiz);
  Serial.print(F(" PD="));  Serial.print(extra.pd);
  Serial.print(F(" PU="));  Serial.print(extra.pu);
  Serial.print(F("  -> "));  Serial.println(interpretPin(extra));

  // One-line diagnosis
  Serial.print(F("  Diagnosis: "));
  bool dpPulledHigh = (dp.pd == 1);
  bool dmPulledHigh = (dm.pd == 1);
  bool dpPulledLow  = (dp.pu == 0);
  bool dmPulledLow  = (dm.pu == 0);

  if (dpPulledHigh && !dmPulledHigh) {
    Serial.println(F("FULL-SPEED DEVICE PRESENT (D+ pulled high) -- bus is healthy"));
  } else if (!dpPulledHigh && dmPulledHigh) {
    Serial.println(F("D-/D+ SWAPPED, or low-speed device. If trackpad is full-speed, swap data lines"));
  } else if (!dpPulledHigh && !dmPulledHigh && !dpPulledLow && !dmPulledLow) {
    Serial.println(F("NO DEVICE VISIBLE -- VBUS missing to trackpad, broken D+ wire, or open series resistor"));
  } else if (dpPulledLow || dmPulledLow) {
    Serial.println(F("LINE BEING DRIVEN LOW externally -- short to GND or damaged component"));
  } else {
    Serial.println(F("ambiguous -- repeat the probe, check connections"));
  }

  Serial.println();
}

// ---------- Phase A2: cross-coupling drive test ----------
//
// Drives one pin LOW with a strong CMOS output, then reads the other with
// the internal pullup engaged. If the pins are shorted (or share a net) the
// reader pin will follow the driver to LOW. If they're independent, the
// reader's pullup keeps it HIGH.
//
// We only ever drive LOW: that lets the RP2040 sink current against an
// external pullup (1.5 kΩ to 3.3/5 V on D+ if a trackpad is attached) without
// fighting an external driver, max ~3.3 mA — well within spec.

int driveLowReadOther(int driverPin, int readerPin) {
  pinMode(readerPin, INPUT_PULLUP);
  delay(2);
  pinMode(driverPin, OUTPUT);
  digitalWrite(driverPin, LOW);
  delay(5);
  int v = digitalRead(readerPin);
  pinMode(driverPin, INPUT);     // release
  pinMode(readerPin, INPUT);
  delay(2);
  return v;
}

// Self-drive verification. Drives the pin LOW with default 4 mA strength, then
// reads it back WHILE STILL DRIVING. If the input buffer is healthy and there's
// no extreme external pullup, this should read 0. If it reads 1, either the
// input buffer is stuck HIGH (silicon damage) or there's a near-short to 3.3/5 V.
int selfDriveLow(int pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delay(5);
  int v = digitalRead(pin);    // pad voltage while still driven LOW
  pinMode(pin, INPUT);
  delay(2);
  return v;
}

int selfDriveHigh(int pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, HIGH);
  delay(5);
  int v = digitalRead(pin);
  pinMode(pin, INPUT);
  delay(2);
  return v;
}

void selfDriveTest() {
  Serial.println(F("---- Phase A3: self-drive verification ----"));
  Serial.println(F("Drive pin LOW (then HIGH) and read back the actual pad voltage."));
  Serial.println(F("Healthy pin: drive LOW -> reads 0 ; drive HIGH -> reads 1."));
  Serial.println(F("Reading 1 while driving LOW => damaged input buffer OR hard short to V+."));
  Serial.println();

  int pins[] = {HOST_PIN_DP, HOST_PIN_DM, EXTRA_PROBE};
  const char* labels[] = {"D+ (GPIO 1)", "D- (GPIO 0)", "GPIO 2     "};
  for (int i = 0; i < 3; i++) {
    int lo = selfDriveLow(pins[i]);
    int hi = selfDriveHigh(pins[i]);
    Serial.print(F("  "));
    Serial.print(labels[i]);
    Serial.print(F(": driveLOW->"));  Serial.print(lo);
    Serial.print(F("  driveHIGH->")); Serial.print(hi);
    if (lo == 0 && hi == 1)       Serial.println(F("   OK (pin is healthy and not shorted to V+)"));
    else if (lo == 1 && hi == 1)  Serial.println(F("   STUCK HIGH (damaged buffer or short to V+) !!"));
    else if (lo == 0 && hi == 0)  Serial.println(F("   STUCK LOW (damaged buffer or short to GND) !!"));
    else                          Serial.println(F("   inconsistent"));
  }
  Serial.println();
}

void crossCouplingTest() {
  Serial.println(F("---- Phase A2: cross-coupling drive test ----"));
  Serial.println(F("Driving each pin LOW, reading the other with internal pullup."));
  Serial.println(F("If pins are independent, reader stays HIGH."));
  Serial.println(F("If pins are shorted, reader follows driver to LOW."));
  Serial.println();

  int dm_when_dp_low = driveLowReadOther(HOST_PIN_DP, HOST_PIN_DM);
  int dp_when_dm_low = driveLowReadOther(HOST_PIN_DM, HOST_PIN_DP);
  int x_when_dp_low  = driveLowReadOther(HOST_PIN_DP, EXTRA_PROBE);
  int x_when_dm_low  = driveLowReadOther(HOST_PIN_DM, EXTRA_PROBE);

  Serial.print(F("  drive D+ LOW, read D-: "));   Serial.print(dm_when_dp_low);
  Serial.println(dm_when_dp_low == 0 ? F("  <-- SHORT D+ <-> D- !!") : F("  (independent)"));

  Serial.print(F("  drive D- LOW, read D+: "));   Serial.print(dp_when_dm_low);
  Serial.println(dp_when_dm_low == 0 ? F("  <-- SHORT D+ <-> D- !!") : F("  (independent)"));

  Serial.print(F("  drive D+ LOW, read GPIO 2: ")); Serial.print(x_when_dp_low);
  Serial.println(x_when_dp_low == 0 ? F("  <-- SHORT D+ <-> GPIO 2") : F("  (independent)"));

  Serial.print(F("  drive D- LOW, read GPIO 2: ")); Serial.print(x_when_dm_low);
  Serial.println(x_when_dm_low == 0 ? F("  <-- SHORT D- <-> GPIO 2") : F("  (independent)"));

  Serial.println();
}

// ---------- Core 0 ----------
void setup() {
  // Hi-Z everything we touch before anything else might claim it.
  pinMode(HOST_PIN_DP, INPUT);
  pinMode(HOST_PIN_DM, INPUT);
  pinMode(EXTRA_PROBE, INPUT);

  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) {}

  Serial.println();
  Serial.println(F("=== BitchBoy trackpad PCB diagnostic (2-phase) ==="));
  Serial.println();
  Serial.println(F("PHASE A: passive line probe (no PIO USB yet)"));
  Serial.println(F("  Plug trackpad into the PCB USB-C port."));
  Serial.println(F("  Expected if healthy + full-speed device:"));
  Serial.println(F("    D+ -> EXTERNALLY PULLED HIGH"));
  Serial.println(F("    D- -> FLOATING"));
  Serial.println();
}

void loop() {
  static unsigned long phaseStart = millis();
  unsigned long elapsed = millis() - phaseStart;

  // ----- Phase A -----
  if (!flagStartPioUsb) {
    if (elapsed >= PHASE_A_MS) {
      Serial.println(F("---- end Phase A1 (passive) ----"));
      Serial.println();
      crossCouplingTest();          // Phase A2: active drive test
      selfDriveTest();              // Phase A3: silicon-vs-PCB discriminator
      Serial.println(F("---- end Phase A ----"));
      Serial.println();
      Serial.println(F("PHASE B: activating PIO USB host..."));
      Serial.println();
      flagStartPioUsb = true;     // signal Core 1 to begin
      return;
    }

    static unsigned long lastProbe = 0;
    if (elapsed - lastProbe >= 500 || lastProbe == 0) {
      lastProbe = elapsed;
      probeRound(elapsed);
    }
    return;
  }

  // ----- Phase B -----
  // Drain HID report queue
  while (qTail != qHead) {
    ReportSlot slot;
    memcpy(&slot, (const void*)&reportQueue[qTail], sizeof(slot));
    Serial.print(F("HID dev=")); Serial.print(slot.devAddr);
    Serial.print(F(" inst="));   Serial.print(slot.instance);
    Serial.print(F(" len="));    Serial.print(slot.len);
    Serial.print(F("  "));
    for (uint8_t i = 0; i < slot.len; i++) {
      if (slot.data[i] < 0x10) Serial.print('0');
      Serial.print(slot.data[i], HEX);
      Serial.print(' ');
    }
    Serial.println();
    qTail = (qTail + 1) % Q_SIZE;
  }

  if (flagCore1Ready) { flagCore1Ready = false; Serial.println(F("Core 1 ready (PIO USB host running)")); }
  if (flagMounted)    { flagMounted    = false;
                        Serial.print(F("MOUNTED dev=")); Serial.print(lastDevAddr);
                        Serial.print(F(" inst="));       Serial.println(lastInstance); }
  if (flagUnmounted)  { flagUnmounted  = false; Serial.println(F("UNMOUNTED")); }

  static unsigned long lastBeat = 0;
  unsigned long now = millis();
  if (now - lastBeat >= 1000) {
    lastBeat = now;
    Serial.print('['); Serial.print(now / 1000);
    Serial.print(F("s] core1_hb=")); Serial.print(core1Heartbeat);
    Serial.print(F("  any_dev_mounted="));
    Serial.println(tuh_mounted(1) ? 1 : 0);
  }
}

// ---------- Core 1 ----------
void setup1() {
  // Wait for Core 0 to finish Phase A before grabbing the pins.
  while (!flagStartPioUsb) { delay(10); }
  delay(200);  // settle

  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = HOST_PIN_DP;
  pio_cfg.pinout = PIO_USB_PINOUT_DMDP;
  USBHost.configure_pio_usb(1, &pio_cfg);
  USBHost.begin(1);

  flagCore1Ready = true;
}

void loop1() {
  if (!flagStartPioUsb) { delay(5); return; }
  USBHost.task();
  core1Heartbeat = millis();
}

// ---------- TinyUSB Host HID callbacks (NO Serial here!) ----------
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const* /*desc*/, uint16_t /*len*/) {
  lastDevAddr  = dev_addr;
  lastInstance = instance;
  flagMounted  = true;
  tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_REPORT);
  tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  (void)dev_addr; (void)instance;
  flagUnmounted = true;
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const* report, uint16_t len) {
  uint8_t nextHead = (qHead + 1) % Q_SIZE;
  if (nextHead != qTail) {
    ReportSlot* slot = (ReportSlot*)&reportQueue[qHead];
    uint8_t copyLen  = (len > sizeof(slot->data)) ? sizeof(slot->data) : (uint8_t)len;
    memcpy(slot->data, report, copyLen);
    slot->len      = copyLen;
    slot->devAddr  = dev_addr;
    slot->instance = instance;
    qHead = nextHead;
  }
  tuh_hid_receive_report(dev_addr, instance);
}
