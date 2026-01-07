/*
 * BitchBoy Firmware - PIO-USB Stability Version
 *
 * USB Device (Core 0): MIDI + HID Mouse + HID Keyboard via TinyUSB
 * USB Host (Core 1): Trackpad via TinyUSB Host + PIO-USB
 *
 * Stability improvements based on official Pico-PIO-USB examples:
 * - CPU clock MUST be multiple of 12MHz (120MHz or 240MHz)
 * - Core 1 must run tuh_task() with minimal interruption
 * - NeoPixel and other blocking operations must not interfere with USB timing
 *
 * IMPORTANT BUILD SETTINGS:
 * For Arduino IDE: Tools -> CPU Speed -> 120 MHz
 * For PlatformIO: board_build.f_cpu = 120000000L in platformio.ini
 *
 * The code also calls set_sys_clock_khz(120000, true) at startup as backup,
 * but IDE settings may override this, so verify with Serial output.
 */

#include <Adafruit_TCA8418.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>
#include <CD74HC4067.h>
#include <MIDI.h>
#include <pio_usb.h>
#include "tusb.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"

// USB Host pin for trackpad (D+ line)
#define HOST_PIN_DP 1

// Trackpad settings
#define TRACKPAD_REPORT_QUEUE_SIZE 8       // Buffer for report queue between cores

// Custom USB Device Descriptor
tusb_desc_device_t custom_desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0xCafe,
    .idProduct          = 0x4002,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

// Custom String Descriptors
const char* custom_string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    "BLOCK SYSTEM",
    "BitchBoy",
    "123456",
};

// Override the default descriptors
extern "C" {
const tusb_desc_device_t* tud_desc_get_custom_device(void) {
    return &custom_desc_device;
}

const char* const* tud_desc_get_custom_string(void) {
    return custom_string_desc_arr;
}
}

// User variables
int MIDI_OUT_CH = 1;

Adafruit_USBD_MIDI usb_midi;
Adafruit_USBH_Host USBHost;
Adafruit_USBD_HID usb_hid;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

// Mouse HID descriptor with scroll wheel for trackpad
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

// Separate keyboard HID for zoom gestures
Adafruit_USBD_HID usb_keyboard;

uint8_t const desc_hid_keyboard[] = {
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x06,        // Usage (Keyboard)
  0xA1, 0x01,        // Collection (Application)
  0x05, 0x07,        //   Usage Page (Key Codes)
  0x19, 0xE0,        //   Usage Minimum (224) - Left Control
  0x29, 0xE7,        //   Usage Maximum (231) - Right GUI
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x01,        //   Logical Maximum (1)
  0x75, 0x01,        //   Report Size (1)
  0x95, 0x08,        //   Report Count (8)
  0x81, 0x02,        //   Input (Data,Var,Abs) - Modifier byte
  0x95, 0x01,        //   Report Count (1)
  0x75, 0x08,        //   Report Size (8)
  0x81, 0x01,        //   Input (Const) - Reserved byte
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

// HID Keyboard keycodes
#define KEY_MOD_LGUI   0x08
#define KEY_MOD_LCTRL  0x01
#define KEY_MOD_LSHIFT 0x02
#define KEY_M          0x10

// Define the number of rows and columns for the keypad
#define ROWS 8
#define COLS 8
#define NUMPIXELS ROWS* COLS

Adafruit_TCA8418 tca8418;
#define TCA8418_ADDR TCA8418_DEFAULT_ADDR

CD74HC4067 mux(22, 21, 19, 20);

#define SLIDERS_PIN A0
#define POTS_PIN A1
#define NUM_SLIDERS 12
#define NUM_POTS 8

const int sliderMap[NUM_SLIDERS] = {12, 11, 10, 9, 8, 7, 6, 5, 1, 2, 3, 4};
const int potMap[NUM_POTS] = {2, 4, 6, 8, 1, 3, 5, 7};

int sliderMinValues[NUM_SLIDERS] = {375, 369, 396, 374, 385, 366, 380, 358, 377, 375, 362, 375};
int sliderMaxValues[NUM_SLIDERS] = {4095, 4095, 4094, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095};

int potMinValues[NUM_POTS] = {355, 352, 377, 379, 369, 390, 383, 384};
int potMaxValues[NUM_POTS] = {4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095};

int previousSliderValues[NUM_SLIDERS] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
int previousPotValues[NUM_POTS] = {-1, -1, -1, -1, -1, -1, -1, -1};

float smoothedSliderValues[NUM_SLIDERS] = {0,0,0,0,0,0,0,0,0,0,0,0};
float smoothedPotValues[NUM_POTS] = {0,0,0,0,0,0,0,0};

char keys[ROWS][COLS] = {
  { '0', '1', '2', '3', '4', '5', 'U', 'V' },
  { '6', '7', '8', '9', 'A', 'B', 'X', 'Y' },
  { 'C', 'D', 'E', 'F', 'G', 'H', 'a', 'b' },
  { 'I', 'J', 'K', 'L', 'M', 'N', 'd', 'e' },
  { 'O', 'P', 'Q', 'R', 'S', 'T', 'g', 'h' },
  { 'W', 'X', 'Y', 'Z', '1', '2', '3', '4' },
  { '5', '6', '7', '8', '9', '0', '-', '=' },
  { 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i' }
};

int padToNote[NUMPIXELS] = {
  64, 65, 66, 67, 96, 97, 98, 99,
  60, 61, 62, 63, 92, 93, 94, 95,
  56, 57, 58, 59, 88, 89, 90, 91,
  52, 53, 54, 55, 84, 85, 86, 87,
  48, 49, 50, 51, 80, 81, 82, 83,
  44, 45, 46, -1, -1, -1, -1, -1,
  40, 41, 42, -1, -1, -1, -1, -1,
  36, 37, 38, -1, -1, -1, -1, -1
};

int padToPixel[NUMPIXELS] = {
  0, 1, 2, 3, 4, 5, 6, 7,
  8, 9, 10, 11, 12, 13, 14, 15,
  16, 17, 18, 19, 20, 21, 22, 23,
  24, 25, 26, 27, 28, 29, 30, 31,
  32, 33, 34, 35, 36, 37, 38, 39,
  40, 41, 42, -1, -1, -1, -1, -1,
  43, 44, 45, -1, -1, -1, -1, -1,
  46, 47, 48, -1, -1, -1, -1, -1
};
int noteToPad[128];
int velocities[128];
#define FLASHING_VELOCITY 40

unsigned long lastMessageTime = 0;
const unsigned long batchTimeout = 10;
bool inBatch = false;
bool keysUpdated[NUMPIXELS];
unsigned int batchMessageCount = 0;
const unsigned int BATCH_MIN_MESSAGE_COUNT = 4;
const int VELOCITY_BUFFER_SIZE = 10;
int velocityBuffer[VELOCITY_BUFFER_SIZE];
int bufferIndex = 0;

#define NEO_PIN 14
Adafruit_NeoPixel keypadPixels(NUMPIXELS, NEO_PIN, NEO_GRB + NEO_KHZ800);
unsigned long previousMillis = 0;
const long interval = 500;
bool flashState = false;

// Trackpad state tracking (volatile for cross-core access)
volatile bool trackpadConnected = false;
volatile uint8_t trackpadDevAddr = 0;
volatile uint8_t trackpadInstance = 0;

// Lock-free queue for trackpad reports (Core 1 -> Core 0)
struct TrackpadReport {
  uint8_t data[5];
  bool valid;
};
volatile TrackpadReport reportQueue[TRACKPAD_REPORT_QUEUE_SIZE];
volatile uint8_t queueWriteIdx = 0;
volatile uint8_t queueReadIdx = 0;

// Pending keyboard report for pinch (non-blocking)
volatile bool pinchPending = false;
volatile uint8_t pinchState = 0;
volatile unsigned long pinchStateTime = 0;
volatile unsigned long lastPinchTime = 0;

void setup() {
  // CRITICAL: Set CPU clock to 120MHz for PIO-USB stability
  // PIO-USB requires clock to be a multiple of 12MHz
  // Default 133MHz causes timing issues!
  set_sys_clock_khz(120000, true);

  // Brief delay after clock change for stability
  delay(10);

  // Reset Core 1 before it starts (pattern from brendena/pico_device_and_host)
  // In Arduino, setup1()/loop1() auto-launch on Core 1, but reset ensures clean state
  multicore_reset_core1();

  TinyUSBDevice.setManufacturerDescriptor("BLOCK SYSTEM");
  TinyUSBDevice.setProductDescriptor("BitchBoy");
  TinyUSBDevice.setSerialDescriptor("123456");
  Serial.begin(115200);

  Wire.begin();
  Wire.setClock(400000);  // Reduced from 1MHz to reduce timing pressure

  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();

  usb_keyboard.setReportDescriptor(desc_hid_keyboard, sizeof(desc_hid_keyboard));
  usb_keyboard.begin();

  MIDI.begin(MIDI_OUT_CH);
  keypadPixels.begin();
  keypadPixels.setBrightness(50);

  if (!tca8418.begin(TCA8418_ADDR)) {
    Serial.println("TCA8418 keypad not found, check wiring & pullups!");
    while (1);
  }

  tca8418.matrix(ROWS, COLS);

  for (int i = 0; i < 128; i++) {
    noteToPad[i] = -1;
    velocities[i] = 0;
  }
  for (int i = 0; i < NUMPIXELS; i++) {
    int note = padToNote[i];
    noteToPad[note] = i;
    keysUpdated[i] = false;
  }

  for (int i = 0; i < VELOCITY_BUFFER_SIZE; i++) {
    velocityBuffer[i] = 0;
  }

  keypadPixels.clear();
  keypadPixels.show();

  analogReadResolution(12);

  Serial.println("Setup complete!");
}

// Track if pixels need update (to avoid unnecessary show() calls)
volatile bool pixelsNeedUpdate = false;
unsigned long lastPixelUpdate = 0;
const unsigned long PIXEL_UPDATE_INTERVAL = 16;  // ~60fps max, don't flood

void loop() {
  unsigned long currentTime = millis();

  // Handle trackpad queue first - low latency for mouse movement
  processTrackpadQueue();
  processPinchKeySequence();

  // Handle keypad - I2C operations
  handleKeypad();

  // Read MIDI input
  readMIDI();

  // Update pot/slider values (rate limited internally)
  updatePotValues();

  // Batch timeout handling
  if (inBatch && (currentTime - lastMessageTime > batchTimeout)) {
    inBatch = false;
    batchMessageCount = 0;
  }

  // Flashing LED update
  if (currentTime - previousMillis >= interval) {
    previousMillis = currentTime;
    flashState = !flashState;
    updateFlashingLEDs();
    pixelsNeedUpdate = true;
  }

  // CRITICAL: Rate-limit NeoPixel updates
  // keypadPixels.show() blocks interrupts and can disrupt PIO-USB timing
  if (pixelsNeedUpdate && (currentTime - lastPixelUpdate >= PIXEL_UPDATE_INTERVAL)) {
    keypadPixels.show();
    pixelsNeedUpdate = false;
    lastPixelUpdate = currentTime;
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

void handleKeypad() {
  if (tca8418.available() > 0) {
    int k = tca8418.getEvent();
    bool pressed = k & 0x80;
    k &= 0x7F;
    k--;
    uint8_t row = k / 10;
    uint8_t col = k % 10;

    int keyIndex = (row * COLS) + col;
    if (keyIndex >= 0 && keyIndex < NUMPIXELS) {
      int midiNote = padToNote[keyIndex];

      if (pressed) {
        MIDI.sendNoteOn(midiNote, 127, MIDI_OUT_CH);
        Serial.print("Button pressed -> Row:");
        Serial.print(row);
        Serial.print(" Col:");
        Serial.print(col);
        Serial.print(" KeyIndex:");
        Serial.print(keyIndex);
        Serial.print(" MIDI Note:");
        Serial.print(midiNote);
        Serial.println(" ON");
      } else {
        MIDI.sendNoteOff(midiNote, 0, MIDI_OUT_CH);
        Serial.print("Button released -> Row:");
        Serial.print(row);
        Serial.print(" Col:");
        Serial.print(col);
        Serial.print(" KeyIndex:");
        Serial.print(keyIndex);
        Serial.print(" MIDI Note:");
        Serial.print(midiNote);
        Serial.println(" OFF");
      }
    }
  }
}

int findChannelForSlider(int sliderNum) {
  for (int i = 0; i < NUM_SLIDERS; i++) {
    if (sliderMap[i] == sliderNum) return i;
  }
  return -1;
}

int findChannelForPot(int potNum) {
  for (int i = 0; i < NUM_POTS; i++) {
    if (potMap[i] == potNum) return i;
  }
  return -1;
}

int readCalibratedSlider(int channel) {
  mux.channel(channel);
  delayMicroseconds(100);

  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(SLIDERS_PIN);
    delayMicroseconds(10);
  }
  int rawValue = sum / 10;

  if (smoothedSliderValues[channel] == 0) smoothedSliderValues[channel] = rawValue;
  smoothedSliderValues[channel] = (smoothedSliderValues[channel] * 0.6) + (rawValue * 0.4);

  int calibratedValue = map((int)smoothedSliderValues[channel], sliderMinValues[channel], sliderMaxValues[channel], 0, 1270);
  calibratedValue = (calibratedValue + 5) / 10;

  if (calibratedValue <= 2) {
    calibratedValue = 0;
  } else if (calibratedValue >= 125) {
    calibratedValue = 127;
  }

  return constrain(calibratedValue, 0, 127);
}

int readCalibratedPot(int channel) {
  mux.channel(channel);
  delayMicroseconds(100);

  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(POTS_PIN);
    delayMicroseconds(10);
  }
  int rawValue = sum / 10;

  if (smoothedPotValues[channel] == 0) smoothedPotValues[channel] = rawValue;
  smoothedPotValues[channel] = (smoothedPotValues[channel] * 0.6) + (rawValue * 0.4);

  int calibratedValue = map((int)smoothedPotValues[channel], potMinValues[channel], potMaxValues[channel], 0, 1270);
  calibratedValue = (calibratedValue + 5) / 10;

  if (calibratedValue <= 2) {
    calibratedValue = 0;
  } else if (calibratedValue >= 125) {
    calibratedValue = 127;
  }

  return constrain(calibratedValue, 0, 127);
}

void updatePotValues() {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 5) {
    lastUpdate = millis();

    for (int sliderNum = 1; sliderNum <= NUM_SLIDERS; sliderNum++) {
      int channel = findChannelForSlider(sliderNum);
      if (channel >= 0) {
        int value = readCalibratedSlider(channel);

        if (abs(value - previousSliderValues[channel]) >= 3) {
          MIDI.sendControlChange(sliderNum - 1, value, MIDI_OUT_CH);
          previousSliderValues[channel] = value;

          Serial.print("Slider #");
          Serial.print(sliderNum);
          Serial.print(" (Channel ");
          Serial.print(channel);
          Serial.print(") -> CC");
          Serial.print(sliderNum - 1);
          Serial.print(" = ");
          Serial.println(value);
        }
      }
    }

    for (int potNum = 1; potNum <= NUM_POTS; potNum++) {
      int channel = findChannelForPot(potNum);
      if (channel >= 0) {
        int value = readCalibratedPot(channel);

        if (abs(value - previousPotValues[channel]) >= 3) {
          MIDI.sendControlChange(20 + potNum - 1, value, MIDI_OUT_CH);
          previousPotValues[channel] = value;

          Serial.print("Pot #");
          Serial.print(potNum);
          Serial.print(" (Channel ");
          Serial.print(channel);
          Serial.print(") -> CC");
          Serial.print(20 + potNum - 1);
          Serial.print(" = ");
          Serial.println(value);
        }
      }
    }
  }
}

void readMIDI() {
  while (usb_midi.available()) {
    uint8_t statusByte = usb_midi.read();
    uint8_t dataByte1 = usb_midi.read();
    uint8_t dataByte2 = usb_midi.read();

    unsigned long currentTime = millis();

    if (currentTime - lastMessageTime > batchTimeout) {
      inBatch = true;
      batchMessageCount = 0;
      for (int i = 0; i < NUMPIXELS; i++) {
        keysUpdated[i] = false;
      }
    }
    lastMessageTime = currentTime;

    batchMessageCount++;

    updateVelocityBuffer(dataByte2);

    uint8_t messageType = statusByte & 0xF0;

    if (messageType == 0x90) {
      int midiNote = dataByte1;
      int velocity = dataByte2;

      int padIndex = noteToPad[midiNote];
      if (padIndex != -1) {
        keysUpdated[padIndex] = true;
        velocities[midiNote] = velocity;
        uint32_t color = velocityToColor(velocity);

        if (velocity != FLASHING_VELOCITY) {
          keypadPixels.setPixelColor(padToPixel[padIndex], color);
          pixelsNeedUpdate = true;
        }
      }
    } else if (messageType == 0x80) {
      int midiNote = dataByte1;
      int padIndex = noteToPad[midiNote];
      if (padIndex != -1) {
        velocities[midiNote] = 0;
        keysUpdated[padIndex] = true;
        keypadPixels.setPixelColor(padToPixel[padIndex], 0);
        pixelsNeedUpdate = true;
      }
    }
  }
}

void updateVelocityBuffer(int velocity) {
  velocityBuffer[bufferIndex] = velocity;
  bufferIndex = (bufferIndex + 1) % VELOCITY_BUFFER_SIZE;
}

bool shouldSkipRefresh() {
  int consecutiveCount = 0;
  for (int i = 0; i < VELOCITY_BUFFER_SIZE; i++) {
    if (velocityBuffer[i] == 95) {
      consecutiveCount++;
      if (consecutiveCount >= 4) {
        return true;
      }
    } else {
      consecutiveCount = 0;
    }
  }
  return false;
}

void updateFlashingLEDs() {
  for (int i = 0; i < NUMPIXELS; i++) {
    int midiNote = padToNote[i];
    int velocity = velocities[midiNote];
    if (velocity == FLASHING_VELOCITY) {
      if (flashState) {
        uint32_t color = velocityToColor(velocity);
        keypadPixels.setPixelColor(padToPixel[i], color);
      } else {
        keypadPixels.setPixelColor(padToPixel[i], 0);
      }
    }
  }
}

uint32_t velocityToColor(int velocity) {
  switch (velocity) {
    case 0: return keypadPixels.Color(0x06, 0x06, 0x06);
    case 1: return keypadPixels.Color(0x1E, 0x1E, 0x1E);
    case 2: return keypadPixels.Color(0x7F, 0x7F, 0x7F);
    case 3: return keypadPixels.Color(0xFF, 0xFF, 0xFF);
    case 4: return keypadPixels.Color(0xFF, 0x4C, 0x4C);
    case 5: return keypadPixels.Color(0xFF, 0x00, 0x00);
    case 6: return keypadPixels.Color(0x59, 0x00, 0x00);
    case 7: return keypadPixels.Color(0x19, 0x00, 0x00);
    case 8: return keypadPixels.Color(0xFF, 0xBD, 0x6C);
    case 9: return keypadPixels.Color(0xFF, 0x54, 0x00);
    case 10: return keypadPixels.Color(0x59, 0x1D, 0x00);
    case 11: return keypadPixels.Color(0x27, 0x1B, 0x00);
    case 12: return keypadPixels.Color(0xFF, 0xFF, 0x4C);
    case 13: return keypadPixels.Color(0xFF, 0xFF, 0x00);
    case 14: return keypadPixels.Color(0x59, 0x59, 0x00);
    case 15: return keypadPixels.Color(0x19, 0x19, 0x00);
    case 16: return keypadPixels.Color(0x88, 0xFF, 0x4C);
    case 17: return keypadPixels.Color(0x54, 0xFF, 0x00);
    case 18: return keypadPixels.Color(0x1D, 0x59, 0x00);
    case 19: return keypadPixels.Color(0x14, 0x2B, 0x00);
    case 20: return keypadPixels.Color(0x4C, 0xFF, 0x4C);
    case 21: return keypadPixels.Color(0x00, 0xFF, 0x00);
    case 22: return keypadPixels.Color(0x00, 0x59, 0x00);
    case 23: return keypadPixels.Color(0x00, 0x19, 0x00);
    case 24: return keypadPixels.Color(0x4C, 0xFF, 0x5E);
    case 25: return keypadPixels.Color(0x00, 0xFF, 0x19);
    case 26: return keypadPixels.Color(0x00, 0x59, 0x0D);
    case 27: return keypadPixels.Color(0x00, 0x19, 0x02);
    case 28: return keypadPixels.Color(0x4C, 0xFF, 0x88);
    case 29: return keypadPixels.Color(0x00, 0xFF, 0x55);
    case 30: return keypadPixels.Color(0x00, 0x59, 0x1D);
    case 31: return keypadPixels.Color(0x00, 0x1F, 0x12);
    case 32: return keypadPixels.Color(0xD6, 0x35, 0x00);
    case 33: return keypadPixels.Color(0x00, 0xFF, 0x99);
    case 34: return keypadPixels.Color(0x00, 0x59, 0x35);
    case 35: return keypadPixels.Color(0x00, 0x19, 0x12);
    case 36: return keypadPixels.Color(0x4C, 0xC3, 0xFF);
    case 37: return keypadPixels.Color(0x00, 0xA9, 0xFF);
    case 38: return keypadPixels.Color(0x00, 0x41, 0x52);
    case 39: return keypadPixels.Color(0x00, 0x10, 0x19);
    case 40: return keypadPixels.Color(0x4C, 0x88, 0xFF);
    case 41: return keypadPixels.Color(0x00, 0x55, 0xFF);
    case 42: return keypadPixels.Color(0x00, 0x1D, 0x59);
    case 43: return keypadPixels.Color(0x00, 0x08, 0x19);
    case 44: return keypadPixels.Color(0x4C, 0x4C, 0xFF);
    case 45: return keypadPixels.Color(0x00, 0x00, 0xFF);
    case 46: return keypadPixels.Color(0x00, 0x00, 0x59);
    case 47: return keypadPixels.Color(0x00, 0x00, 0x19);
    case 48: return keypadPixels.Color(0x87, 0x4C, 0xFF);
    case 49: return keypadPixels.Color(0x54, 0x00, 0xFF);
    case 50: return keypadPixels.Color(0x19, 0x00, 0x64);
    case 51: return keypadPixels.Color(0x0F, 0x00, 0x30);
    case 52: return keypadPixels.Color(0xFF, 0x4C, 0xFF);
    case 53: return keypadPixels.Color(0xFF, 0x00, 0xFF);
    case 54: return keypadPixels.Color(0x59, 0x00, 0x59);
    case 55: return keypadPixels.Color(0x19, 0x00, 0x19);
    case 56: return keypadPixels.Color(0xFF, 0x4C, 0x87);
    case 57: return keypadPixels.Color(0xFF, 0x00, 0x54);
    case 58: return keypadPixels.Color(0x59, 0x00, 0x1D);
    case 59: return keypadPixels.Color(0x22, 0x00, 0x13);
    case 60: return keypadPixels.Color(0xFF, 0x15, 0x00);
    case 61: return keypadPixels.Color(0x99, 0x35, 0x00);
    case 62: return keypadPixels.Color(0x79, 0x51, 0x00);
    case 63: return keypadPixels.Color(0x43, 0x64, 0x00);
    case 64: return keypadPixels.Color(0x18, 0x18, 0x00);
    case 65: return keypadPixels.Color(0x00, 0x57, 0x35);
    case 66: return keypadPixels.Color(0x00, 0x54, 0x7F);
    case 67: return keypadPixels.Color(0x00, 0x00, 0xFF);
    case 68: return keypadPixels.Color(0x00, 0x45, 0x4F);
    case 69: return keypadPixels.Color(0x25, 0x00, 0xCC);
    case 70: return keypadPixels.Color(0x7F, 0x7F, 0x7F);
    case 71: return keypadPixels.Color(0x20, 0x20, 0x20);
    case 72: return keypadPixels.Color(0xFF, 0x00, 0x00);
    case 73: return keypadPixels.Color(0xBD, 0xFF, 0x2D);
    case 74: return keypadPixels.Color(0xAF, 0xED, 0x06);
    case 75: return keypadPixels.Color(0x64, 0xFF, 0x09);
    case 76: return keypadPixels.Color(0x10, 0x8B, 0x00);
    case 77: return keypadPixels.Color(0x00, 0xFF, 0x87);
    case 78: return keypadPixels.Color(0x00, 0xA9, 0xFF);
    case 79: return keypadPixels.Color(0x00, 0x2A, 0xFF);
    case 80: return keypadPixels.Color(0x3F, 0x00, 0xFF);
    case 81: return keypadPixels.Color(0x7A, 0x00, 0xFF);
    case 82: return keypadPixels.Color(0xB2, 0x1A, 0x7D);
    case 83: return keypadPixels.Color(0x40, 0x21, 0x00);
    case 84: return keypadPixels.Color(0xFF, 0x4A, 0x00);
    case 85: return keypadPixels.Color(0x88, 0xE1, 0x06);
    case 86: return keypadPixels.Color(0x72, 0xFF, 0x15);
    case 87: return keypadPixels.Color(0x00, 0xFF, 0x00);
    case 88: return keypadPixels.Color(0x3B, 0xFF, 0x26);
    case 89: return keypadPixels.Color(0x59, 0xFF, 0x71);
    case 90: return keypadPixels.Color(0x38, 0xFF, 0xCC);
    case 91: return keypadPixels.Color(0x5B, 0x8A, 0xFF);
    case 92: return keypadPixels.Color(0x31, 0x51, 0xC6);
    case 93: return keypadPixels.Color(0x87, 0x7F, 0xE9);
    case 94: return keypadPixels.Color(0xD3, 0x1D, 0xFF);
    case 95: return keypadPixels.Color(0x00, 0x20, 0x00);
    case 96: return keypadPixels.Color(0xFF, 0x7F, 0x00);
    case 97: return keypadPixels.Color(0xB9, 0xB0, 0x00);
    case 98: return keypadPixels.Color(0x90, 0xFF, 0x00);
    case 99: return keypadPixels.Color(0x83, 0x5D, 0x07);
    case 100: return keypadPixels.Color(0x39, 0x2B, 0x00);
    case 101: return keypadPixels.Color(0x14, 0x4C, 0x10);
    case 102: return keypadPixels.Color(0x0D, 0x50, 0x38);
    case 103: return keypadPixels.Color(0x15, 0x15, 0x2A);
    case 104: return keypadPixels.Color(0x16, 0x20, 0x5A);
    case 105: return keypadPixels.Color(0x69, 0x3C, 0x1C);
    case 106: return keypadPixels.Color(0xA8, 0x00, 0x0A);
    case 107: return keypadPixels.Color(0xDE, 0x51, 0x3D);
    case 108: return keypadPixels.Color(0xD8, 0x6A, 0x1C);
    case 109: return keypadPixels.Color(0xFF, 0xE1, 0x26);
    case 110: return keypadPixels.Color(0x9E, 0xE1, 0x2F);
    case 111: return keypadPixels.Color(0x67, 0xB5, 0x0F);
    case 112: return keypadPixels.Color(0x1E, 0x1E, 0x30);
    case 113: return keypadPixels.Color(0xDC, 0xFF, 0x6B);
    case 114: return keypadPixels.Color(0x80, 0xFF, 0xBD);
    case 115: return keypadPixels.Color(0x9A, 0x99, 0xFF);
    case 116: return keypadPixels.Color(0x8E, 0x66, 0xFF);
    case 117: return keypadPixels.Color(0x40, 0x40, 0x40);
    case 118: return keypadPixels.Color(0x75, 0x75, 0x75);
    case 119: return keypadPixels.Color(0xE0, 0xFF, 0xFF);
    case 120: return keypadPixels.Color(0xA0, 0x00, 0x00);
    case 121: return keypadPixels.Color(0x35, 0x00, 0x00);
    case 122: return keypadPixels.Color(0x1A, 0xD0, 0x00);
    case 123: return keypadPixels.Color(0x07, 0x42, 0x00);
    case 124: return keypadPixels.Color(0xB9, 0xB0, 0x00);
    case 125: return keypadPixels.Color(0x3F, 0x31, 0x00);
    case 126: return keypadPixels.Color(0xB3, 0x5F, 0x00);
    case 127: return keypadPixels.Color(0x00, 0x20, 0x00);
    default: return keypadPixels.Color(0x1E, 0x1E, 0x1E);
  }
}

// ============================================================================
// Core 1: USB Host for Trackpad
// ============================================================================

// Queue a report for Core 0 to send (lock-free single producer)
bool queueTrackpadReport(const uint8_t* data) {
  uint8_t nextIdx = (queueWriteIdx + 1) % TRACKPAD_REPORT_QUEUE_SIZE;
  if (nextIdx == queueReadIdx) {
    return false;  // Queue full
  }
  memcpy((void*)reportQueue[queueWriteIdx].data, data, 5);
  reportQueue[queueWriteIdx].valid = true;
  queueWriteIdx = nextIdx;
  return true;
}

// Trigger pinch key sequence (non-blocking, processed on Core 0)
void triggerPinchKey() {
  unsigned long now = millis();
  const unsigned long PINCH_RATE_LIMIT_MS = 500;

  if (!pinchPending && (now - lastPinchTime >= PINCH_RATE_LIMIT_MS)) {
    pinchPending = true;
    pinchState = 0;
    pinchStateTime = now;
    lastPinchTime = now;
  }
}

void setup1() {
  // Short delay - matches pattern from brendena/pico_device_and_host
  // Core 0 should have set clock by now
  delay(100);

  // Initialize report queue BEFORE any USB operations
  for (int i = 0; i < TRACKPAD_REPORT_QUEUE_SIZE; i++) {
    reportQueue[i].valid = false;
  }

  // Verify clock frequency - PIO-USB requires multiple of 12MHz
  uint32_t freq = clock_get_hz(clk_sys);
  Serial.print("[Core1] CPU Clock: ");
  Serial.print(freq / 1000);
  Serial.println(" kHz");

  if (freq % 12000000 != 0) {
    Serial.println("[Core1] WARNING: Clock not multiple of 12MHz - PIO-USB may be unstable!");
  }

  // Configure PIO-USB BEFORE init (required order per Pico-PIO-USB docs)
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = HOST_PIN_DP;
  pio_cfg.pinout = PIO_USB_PINOUT_DMDP;
  USBHost.configure_pio_usb(1, &pio_cfg);
  USBHost.begin(1);

  Serial.println("[Core1] USB Host ready");
}

void loop1() {
  // Just call task() - no recovery logic, no status prints
  // PIO USB is timing-sensitive and needs uninterrupted execution
  USBHost.task();
}

// TinyUSB Host HID callbacks
// IMPORTANT: Minimize work and Serial output here - PIO USB is timing-sensitive

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
  trackpadConnected = true;
  trackpadDevAddr = dev_addr;
  trackpadInstance = instance;

  tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_REPORT);
  tuh_hid_receive_report(dev_addr, instance);

  // Single print only on connect
  Serial.println("[Core1] Trackpad connected");
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  trackpadConnected = false;
  trackpadDevAddr = 0;
  trackpadInstance = 0;

  // Single print only on disconnect
  Serial.println("[Core1] Trackpad disconnected");
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  // Process report - NO Serial output here, it disrupts PIO timing
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
    uint8_t flags = report[1];
    uint8_t gestureType = report[3];

    if ((flags & 0x08) && (gestureType == 0x56 || gestureType == 0x57)) {
      triggerPinchKey();
    }
  }

  // Request next report - just call it, don't check result or print
  tuh_hid_receive_report(dev_addr, instance);
}
