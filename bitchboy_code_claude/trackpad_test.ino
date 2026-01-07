/*
 * Trackpad Test - Minimal PIO-USB Host Test
 *
 * This matches the working bitchboy_main.ino pattern exactly.
 * Core 0: USB Device (HID Mouse output only)
 * Core 1: USB Host (trackpad via PIO-USB)
 *
 * No MIDI, no keypad, no pots, no NeoPixels - just trackpad passthrough.
 */

#include <Adafruit_TinyUSB.h>
#include <pio_usb.h>
#include "tusb.h"

// USB Host pin for trackpad (D+ line)
#define HOST_PIN_DP 1

// Queue for trackpad reports (Core 1 -> Core 0)
#define TRACKPAD_REPORT_QUEUE_SIZE 8

struct TrackpadReport {
  uint8_t data[5];
  bool valid;
};
volatile TrackpadReport reportQueue[TRACKPAD_REPORT_QUEUE_SIZE];
volatile uint8_t queueWriteIdx = 0;
volatile uint8_t queueReadIdx = 0;

// Trackpad state
volatile bool trackpadConnected = false;
volatile unsigned long lastTrackpadActivity = 0;
volatile uint8_t trackpadDevAddr = 0;
volatile uint8_t trackpadInstance = 0;

// Flags for Core 0 to print status messages (avoids mutex deadlock)
// See: https://github.com/adafruit/Adafruit_TinyUSB_Arduino/issues/238
volatile bool flagCore1Ready = false;
volatile bool flagTrackpadConnected = false;
volatile bool flagTrackpadDisconnected = false;

// Heartbeat to detect Core 1 freeze (known PIO-USB bug)
// See: https://github.com/sekigon-gonnoc/Pico-PIO-USB/issues/192
volatile unsigned long core1Heartbeat = 0;
volatile bool core1Frozen = false;
#define CORE1_TIMEOUT_MS 2000  // If no heartbeat for 2 seconds, Core 1 is frozen

// Pinch gesture handling
volatile bool pinchPending = false;
volatile uint8_t pinchState = 0;
volatile unsigned long pinchStateTime = 0;

// USB Host
Adafruit_USBH_Host USBHost;

// USB HID Device (Mouse)
Adafruit_USBD_HID usb_hid;

// Mouse HID descriptor with scroll wheel (same as bitchboy_main.ino)
uint8_t const desc_hid_report[] = {
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x02,        // Usage (Mouse)
  0xA1, 0x01,        // Collection (Application)
  0x09, 0x01,        //   Usage (Pointer)
  0xA1, 0x00,        //   Collection (Physical)
  0x05, 0x09,        //     Usage Page (Button)
  0x19, 0x01,        //     Usage Minimum (1)
  0x29, 0x05,        //     Usage Maximum (5)
  0x15, 0x00,        //     Logical Minimum (0)
  0x25, 0x01,        //     Logical Maximum (1)
  0x95, 0x05,        //     Report Count (5)
  0x75, 0x01,        //     Report Size (1)
  0x81, 0x02,        //     Input (Data,Var,Abs)
  0x95, 0x01,        //     Report Count (1)
  0x75, 0x03,        //     Report Size (3)
  0x81, 0x01,        //     Input (Const)
  0x05, 0x01,        //     Usage Page (Generic Desktop)
  0x09, 0x30,        //     Usage (X)
  0x09, 0x31,        //     Usage (Y)
  0x09, 0x38,        //     Usage (Wheel)
  0x15, 0x81,        //     Logical Minimum (-127)
  0x25, 0x7F,        //     Logical Maximum (127)
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x03,        //     Report Count (3)
  0x81, 0x06,        //     Input (Data,Var,Rel)
  0x05, 0x0C,        //     Usage Page (Consumer)
  0x0A, 0x38, 0x02,  //     Usage (AC Pan)
  0x15, 0x81,        //     Logical Minimum (-127)
  0x25, 0x7F,        //     Logical Maximum (127)
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x01,        //     Report Count (1)
  0x81, 0x06,        //     Input (Data,Var,Rel)
  0xC0,              //   End Collection
  0xC0               // End Collection
};

// Keyboard HID for pinch gestures
Adafruit_USBD_HID usb_keyboard;

uint8_t const desc_hid_keyboard[] = {
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x06,        // Usage (Keyboard)
  0xA1, 0x01,        // Collection (Application)
  0x05, 0x07,        //   Usage Page (Key Codes)
  0x19, 0xE0,        //   Usage Minimum (224)
  0x29, 0xE7,        //   Usage Maximum (231)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x01,        //   Logical Maximum (1)
  0x75, 0x01,        //   Report Size (1)
  0x95, 0x08,        //   Report Count (8)
  0x81, 0x02,        //   Input (Data,Var,Abs)
  0x95, 0x01,        //   Report Count (1)
  0x75, 0x08,        //   Report Size (8)
  0x81, 0x01,        //   Input (Const)
  0x95, 0x06,        //   Report Count (6)
  0x75, 0x08,        //   Report Size (8)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x65,        //   Logical Maximum (101)
  0x05, 0x07,        //   Usage Page (Key Codes)
  0x19, 0x00,        //   Usage Minimum (0)
  0x29, 0x65,        //   Usage Maximum (101)
  0x81, 0x00,        //   Input (Data,Array)
  0xC0               // End Collection
};

#define KEY_MOD_LGUI   0x08
#define KEY_MOD_LCTRL  0x01
#define KEY_MOD_LSHIFT 0x02
#define KEY_M          0x10

// ============================================================================
// Core 0: Setup and Loop
// ============================================================================

void setup() {
  // Match bitchboy_main.ino exactly - no clock changes
  TinyUSBDevice.setManufacturerDescriptor("BLOCK SYSTEM");
  TinyUSBDevice.setProductDescriptor("BitchBoy Test");
  TinyUSBDevice.setSerialDescriptor("123456");

  Serial.begin(115200);

  // Initialize USB HID for trackpad mouse output
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();

  // Initialize USB HID keyboard for zoom gestures
  usb_keyboard.setReportDescriptor(desc_hid_keyboard, sizeof(desc_hid_keyboard));
  usb_keyboard.begin();

  Serial.println("=== Trackpad Test ===");
  Serial.println("Setup complete - waiting for trackpad...");
}

void loop() {
  // Process queued trackpad reports (from Core 1)
  processTrackpadQueue();

  // Handle non-blocking pinch key sequence
  processPinchKeySequence();

  // Print status messages from Core 1 (avoids mutex deadlock)
  if (flagCore1Ready) {
    flagCore1Ready = false;
    Serial.println("USB Host ready on Core 1");
  }
  if (flagTrackpadConnected) {
    flagTrackpadConnected = false;
    Serial.println("Trackpad connected");
    core1Frozen = false;  // Reset frozen flag on new connection
  }
  if (flagTrackpadDisconnected) {
    flagTrackpadDisconnected = false;
    Serial.println("Trackpad disconnected");
  }

  // Check for Core 1 freeze (known PIO-USB bug)
  if (trackpadConnected && !core1Frozen) {
    unsigned long now = millis();
    if (now - core1Heartbeat > CORE1_TIMEOUT_MS) {
      core1Frozen = true;
      Serial.println("WARNING: Core 1 appears frozen (PIO-USB bug)");
      Serial.println("Unplug and replug trackpad to recover");
    }
  }
}

void processTrackpadQueue() {
  while (queueReadIdx != queueWriteIdx) {
    uint8_t idx = queueReadIdx;
    if (reportQueue[idx].valid) {
      if (usb_hid.ready()) {
        uint8_t report[5];
        memcpy(report, (void*)reportQueue[idx].data, 5);
        usb_hid.sendReport(0, report, 5);
      }
      reportQueue[idx].valid = false;
    }
    queueReadIdx = (queueReadIdx + 1) % TRACKPAD_REPORT_QUEUE_SIZE;
  }
}

void processPinchKeySequence() {
  if (!pinchPending) return;

  unsigned long now = millis();
  if (now - pinchStateTime < 10) return;

  uint8_t keyReport[8] = {0, 0, 0, 0, 0, 0, 0, 0};

  switch (pinchState) {
    case 0:
      keyReport[0] = KEY_MOD_LCTRL | KEY_MOD_LSHIFT;
      keyReport[2] = KEY_M;
      usb_keyboard.sendReport(0, keyReport, 8);
      pinchState = 1;
      pinchStateTime = now;
      break;
    case 1:
      usb_keyboard.sendReport(0, keyReport, 8);
      pinchState = 2;
      pinchStateTime = now;
      break;
    case 2:
      keyReport[0] = KEY_MOD_LGUI | KEY_MOD_LSHIFT;
      keyReport[2] = KEY_M;
      usb_keyboard.sendReport(0, keyReport, 8);
      pinchState = 3;
      pinchStateTime = now;
      break;
    case 3:
      usb_keyboard.sendReport(0, keyReport, 8);
      pinchState = 0;
      pinchPending = false;
      break;
  }
}

// ============================================================================
// Core 1: USB Host for Trackpad (same as bitchboy_main.ino)
// ============================================================================

bool queueTrackpadReport(const uint8_t* data) {
  uint8_t nextIdx = (queueWriteIdx + 1) % TRACKPAD_REPORT_QUEUE_SIZE;
  if (nextIdx == queueReadIdx) {
    return false;
  }
  memcpy((void*)reportQueue[queueWriteIdx].data, data, 5);
  reportQueue[queueWriteIdx].valid = true;
  queueWriteIdx = nextIdx;
  return true;
}

void triggerPinchKey() {
  if (!pinchPending) {
    pinchPending = true;
    pinchState = 0;
    pinchStateTime = millis();
  }
}

void setup1() {
  delay(1000);  // Wait for Core 0 to initialize

  // Initialize queue
  for (int i = 0; i < TRACKPAD_REPORT_QUEUE_SIZE; i++) {
    reportQueue[i].valid = false;
  }

  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = HOST_PIN_DP;
  pio_cfg.pinout = PIO_USB_PINOUT_DMDP;
  USBHost.configure_pio_usb(1, &pio_cfg);
  USBHost.begin(1);

  // Signal Core 0 instead of Serial.println (avoids potential mutex issues)
  flagCore1Ready = true;
}

void loop1() {
  USBHost.task();

  // Update heartbeat so Core 0 knows we're alive
  core1Heartbeat = millis();
}

// ============================================================================
// TinyUSB Host HID Callbacks
// CRITICAL: NO Serial.println() here! It causes mutex deadlock with Core 0 USB
// See: https://github.com/adafruit/Adafruit_TinyUSB_Arduino/issues/238
// ============================================================================

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
  // NO Serial here - causes deadlock!
  trackpadDevAddr = dev_addr;
  trackpadInstance = instance;
  trackpadConnected = true;
  lastTrackpadActivity = millis();
  flagTrackpadConnected = true;  // Signal Core 0 to print

  tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_REPORT);
  tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  // NO Serial here - causes deadlock!
  trackpadConnected = false;
  trackpadDevAddr = 0;
  trackpadInstance = 0;
  flagTrackpadDisconnected = true;  // Signal Core 0 to print
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  lastTrackpadActivity = millis();

  if (report == nullptr || len == 0) {
    tuh_hid_receive_report(dev_addr, instance);
    return;
  }

  if (len == 6 && report[0] == 0x06) {
    // Normal movement + scroll
    uint8_t report_out[5];
    report_out[0] = report[1];  // buttons
    report_out[1] = report[2];  // X
    report_out[2] = report[3];  // Y
    report_out[3] = report[4];  // vertical scroll
    report_out[4] = report[5];  // horizontal scroll
    queueTrackpadReport(report_out);
  }
  else if (len == 4 && report[0] == 0x08) {
    // Pinch gesture
    static unsigned long lastPinchTime = 0;
    const unsigned long PINCH_RATE_LIMIT_MS = 500;

    uint8_t flags = report[1];
    uint8_t gestureType = report[3];

    if (flags & 0x08) {
      unsigned long now = millis();
      if (now - lastPinchTime >= PINCH_RATE_LIMIT_MS) {
        if (gestureType == 0x56 || gestureType == 0x57) {
          triggerPinchKey();
          lastPinchTime = now;
        }
      }
    }
  }

  if (!tuh_hid_receive_report(dev_addr, instance)) {
    // Failed - device may have disconnected
  }
}
