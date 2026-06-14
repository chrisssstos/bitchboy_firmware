#include <Adafruit_TCA8418.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>
#include <CD74HC4067.h>
#include <MIDI.h>
#include <Wire.h>
#include <pio_usb.h>
#include "tusb.h"

// ============================================================
// Cirque Gen6 Trackpad (I2C-HID, shared bus with TCA8418)
// ============================================================
#define CIRQUE_SDA_PIN 4
#define CIRQUE_SCL_PIN 5
#define CIRQUE_DR_PIN  6    // Data Ready: active LOW
#define CIRQUE_ADDR    0x2C // Cirque "ALPS" I2C address

// Cirque I2C-HID register addresses
#define CIRQUE_HID_DESC_REG  0x0020
#define CIRQUE_CMD_REG       0x0005
#define CIRQUE_DATA_REG      0x0006
#define CIRQUE_MAX_PACKET    53

// Cirque report IDs
#define CIRQUE_RID_MOUSE 0x06

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

const char* custom_string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    "BLOCK SYSTEM",
    "BitchBoy",
    "123456",
};

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
Adafruit_USBD_HID usb_hid;  // Mouse output
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

// ============================================================
// Mouse HID Descriptor (cross-platform: Mac + Windows + Linux)
// ============================================================
uint8_t const desc_hid_mouse[] = {
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
  0x81, 0x01,        //     Input (Const) - padding
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

// ============================================================
// Keypad config
// ============================================================
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

// ============================================================
// Cirque Trackpad State
// ============================================================
static uint32_t cirque_frame_count = 0;
static uint32_t cirque_mouse_count = 0;
static uint32_t cirque_fail_count  = 0;
static bool     cirque_found       = false;

// USB connection stability
bool usbWasMounted = false;
unsigned long usbReconnectTime = 0;
#define USB_SETTLE_MS 150

// ============================================================
// Cirque I2C-HID helpers
// ============================================================

// Read from a 16-bit register (I2C-HID: LSB first)
bool cirque_read_reg(uint16_t reg, uint8_t* out, size_t n) {
  Wire.beginTransmission(CIRQUE_ADDR);
  Wire.write(uint8_t(reg & 0xFF));
  Wire.write(uint8_t((reg >> 8) & 0xFF));
  uint8_t err = Wire.endTransmission(false);
  if (err != 0) return false;
  size_t got = Wire.requestFrom(CIRQUE_ADDR, (uint8_t)n);
  if (got != n) return false;
  for (size_t i = 0; i < n; i++) out[i] = Wire.read();
  return true;
}

// Plain read — Cirque pushes data when DR is LOW
int cirque_read_report(uint8_t* buf, size_t maxlen) {
  size_t got = Wire.requestFrom(CIRQUE_ADDR, (uint8_t)maxlen);
  if (got < 2) return -1;
  for (size_t i = 0; i < got; i++) buf[i] = Wire.read();
  uint16_t len = buf[0] | (buf[1] << 8);
  if (len == 0) return 0;  // HID Reset Response
  if (len > got) return -1;
  return (int)len;
}

// Drain pending reports
void cirque_drain() {
  uint8_t buf[CIRQUE_MAX_PACKET];
  for (int i = 0; i < 10; i++) {
    if (digitalRead(CIRQUE_DR_PIN) != LOW) break;
    cirque_read_report(buf, sizeof(buf));
    delay(2);
  }
}

// ============================================================
// Setup
// ============================================================
void setup() {
  TinyUSBDevice.setManufacturerDescriptor("BLOCK SYSTEM");
  TinyUSBDevice.setProductDescriptor("BitchBoy");
  TinyUSBDevice.setSerialDescriptor("123456");
  Serial.begin(115200);

  // Wait for Serial Monitor (up to 3 seconds, then continue anyway)
  unsigned long serialWait = millis();
  while (!Serial && (millis() - serialWait < 3000)) delay(10);

  Serial.println();
  Serial.println("========================================");
  Serial.println("  BitchBoy Firmware - Cirque Trackpad");
  Serial.println("========================================");

  // I2C bus
  Serial.print("[INIT] I2C SDA=GP");
  Serial.print(CIRQUE_SDA_PIN);
  Serial.print(" SCL=GP");
  Serial.println(CIRQUE_SCL_PIN);
  Wire.setSDA(CIRQUE_SDA_PIN);
  Wire.setSCL(CIRQUE_SCL_PIN);
  Wire.begin();
  Wire.setClock(400000);
  Wire.setTimeout(50);  // 50ms timeout — prevents I2C hang from killing USB

  // Cirque DR pin (active LOW)
  pinMode(CIRQUE_DR_PIN, INPUT_PULLUP);

  // Mouse HID (cross-platform)
  usb_hid.setReportDescriptor(desc_hid_mouse, sizeof(desc_hid_mouse));
  usb_hid.begin();
  Serial.println("[INIT] Mouse HID started");

  MIDI.begin(MIDI_OUT_CH);
  keypadPixels.begin();
  keypadPixels.setBrightness(50);

  if (!tca8418.begin(TCA8418_ADDR)) {
    Serial.println("[INIT] ERROR: TCA8418 not found!");
    while (1);
  }
  Serial.println("[INIT] TCA8418 OK");
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
  for (int i = 0; i < VELOCITY_BUFFER_SIZE; i++) velocityBuffer[i] = 0;

  keypadPixels.clear();
  keypadPixels.show();
  analogReadResolution(12);

  // ---- Cirque Init ----
  Serial.println("[CIRQUE] Scanning I2C...");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("[CIRQUE]   0x");
      if (addr < 16) Serial.print("0");
      Serial.print(addr, HEX);
      if (addr == CIRQUE_ADDR) { Serial.print(" <- Cirque"); cirque_found = true; }
      if (addr == TCA8418_ADDR) Serial.print(" <- TCA8418");
      Serial.println();
    }
  }

  if (!cirque_found) {
    Serial.println("[CIRQUE] WARNING: Cirque NOT found at 0x2C!");
    Serial.println("[CIRQUE] Trackpad will not work. Check wiring.");
  } else {
    // Read HID descriptor
    uint8_t hidDesc[30];
    if (cirque_read_reg(CIRQUE_HID_DESC_REG, hidDesc, sizeof(hidDesc))) {
      uint16_t vid = hidDesc[20] | (hidDesc[21] << 8);
      uint16_t pid = hidDesc[22] | (hidDesc[23] << 8);
      Serial.print("[CIRQUE] VID=0x");
      Serial.print(vid, HEX);
      Serial.print(" PID=0x");
      Serial.println(pid, HEX);
    } else {
      Serial.println("[CIRQUE] WARN: Could not read HID descriptor");
    }

    // Drain boot reports
    delay(100);
    cirque_drain();

    // Cirque defaults to mouse mode — no mode switch needed!
    Serial.println("[CIRQUE] Mouse mode (default)");
  }

  Serial.print("[INIT] DR pin=");
  Serial.println(digitalRead(CIRQUE_DR_PIN) ? "HIGH (idle)" : "LOW (data!)");
  Serial.println("========================================");
  Serial.println("  Setup complete!");
  Serial.println("========================================");
}

// ============================================================
// Loop
// ============================================================
bool usbDeviceReady() {
  if (!TinyUSBDevice.mounted()) return false;
  if (millis() - usbReconnectTime < USB_SETTLE_MS) return false;
  return true;
}

void loop() {
  unsigned long currentTime = millis();

  // USB mount tracking
  bool mounted = TinyUSBDevice.mounted();
  if (mounted && !usbWasMounted) {
    usbReconnectTime = millis();
    Serial.println("USB reconnected");
  }
  if (!mounted && usbWasMounted) Serial.println("USB disconnected");
  usbWasMounted = mounted;

  // Keypad
  handleKeypad();

  // MIDI in
  readMIDI();

  // Pots and sliders
  updatePotValues();

  // Cirque trackpad: DR LOW = data ready
  if (cirque_found && digitalRead(CIRQUE_DR_PIN) == LOW) {
    uint8_t buf[CIRQUE_MAX_PACKET];
    int len = cirque_read_report(buf, sizeof(buf));

    if (len > 2) {
      cirque_frame_count++;
      uint8_t reportId = buf[2];

      if (reportId == CIRQUE_RID_MOUSE && len >= 7) {
        // Cirque mouse report: [len_lo][len_hi][0x06][buttons][X][Y][scroll][pan?]
        cirque_mouse_count++;

        uint8_t buttons = buf[3];
        int8_t dx       = (int8_t)buf[4];
        int8_t dy       = (int8_t)buf[5];
        int8_t scroll   = (int8_t)buf[6];
        int8_t pan      = (len >= 8) ? (int8_t)buf[7] : 0;

        // Debug: print first few and then periodically
        if (cirque_mouse_count <= 5 || (cirque_mouse_count % 500 == 0)) {
          Serial.print("[MOUSE] #");
          Serial.print(cirque_mouse_count);
          Serial.print(" btn=0x");
          Serial.print(buttons, HEX);
          Serial.print(" dx=");
          Serial.print(dx);
          Serial.print(" dy=");
          Serial.print(dy);
          Serial.print(" scr=");
          Serial.print(scroll);
          Serial.print(" pan=");
          Serial.println(pan);
        }

        // Forward to USB HID mouse
        if (usbDeviceReady() && usb_hid.ready()) {
          uint8_t report[5] = { buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)scroll, (uint8_t)pan };
          usb_hid.sendReport(0, report, sizeof(report));
        }

      } else {
        // Unknown or other report
        if (cirque_frame_count <= 10) {
          Serial.print("[CIRQUE] Report ID=0x");
          Serial.print(reportId, HEX);
          Serial.print(" len=");
          Serial.println(len);
        }
      }
    } else if (len == 0) {
      Serial.println("[CIRQUE] Reset response");
    } else if (len < 0) {
      cirque_fail_count++;
      if (cirque_fail_count <= 5) Serial.println("[CIRQUE] Read failed");
    }
  }

  // Status every 5 seconds
  static unsigned long lastStatus = 0;
  if (currentTime - lastStatus >= 5000) {
    lastStatus = currentTime;
    Serial.print("[STATUS] DR=");
    Serial.print(digitalRead(CIRQUE_DR_PIN) ? "HIGH" : "LOW");
    Serial.print(" frames=");
    Serial.print(cirque_frame_count);
    Serial.print(" mouse=");
    Serial.print(cirque_mouse_count);
    Serial.print(" fails=");
    Serial.print(cirque_fail_count);
    Serial.print(" hid_ready=");
    Serial.print(usb_hid.ready() ? "Y" : "N");
    Serial.print(" usb=");
    Serial.println(TinyUSBDevice.mounted() ? "Y" : "N");
  }

  // Batch timeout for LED refresh
  if (inBatch && (currentTime - lastMessageTime > batchTimeout)) {
    inBatch = false;
    batchMessageCount = 0;
  }

  // Flashing LEDs
  if (currentTime - previousMillis >= interval) {
    previousMillis = currentTime;
    flashState = !flashState;
    updateFlashingLEDs();
  }

  // Throttle NeoPixel updates
  static unsigned long lastPixelShow = 0;
  if (currentTime - lastPixelShow >= 8) {
    lastPixelShow = currentTime;
    keypadPixels.show();
  }
}

// ============================================================
// Keypad
// ============================================================
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
        if (usbDeviceReady()) MIDI.sendNoteOn(midiNote, 127, MIDI_OUT_CH);
        Serial.print("Key ON row=");
        Serial.print(row);
        Serial.print(" col=");
        Serial.print(col);
        Serial.print(" note=");
        Serial.println(midiNote);
      } else {
        if (usbDeviceReady()) MIDI.sendNoteOff(midiNote, 0, MIDI_OUT_CH);
        Serial.print("Key OFF row=");
        Serial.print(row);
        Serial.print(" col=");
        Serial.print(col);
        Serial.print(" note=");
        Serial.println(midiNote);
      }
    }
  }
}

// ============================================================
// Sliders & Pots
// ============================================================
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
  if (calibratedValue <= 2) calibratedValue = 0;
  else if (calibratedValue >= 125) calibratedValue = 127;
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
  if (calibratedValue <= 2) calibratedValue = 0;
  else if (calibratedValue >= 125) calibratedValue = 127;
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
          if (usbDeviceReady()) MIDI.sendControlChange(sliderNum - 1, value, MIDI_OUT_CH);
          previousSliderValues[channel] = value;
        }
      }
    }
    for (int potNum = 1; potNum <= NUM_POTS; potNum++) {
      int channel = findChannelForPot(potNum);
      if (channel >= 0) {
        int value = readCalibratedPot(channel);
        if (abs(value - previousPotValues[channel]) >= 3) {
          if (usbDeviceReady()) MIDI.sendControlChange(20 + potNum - 1, value, MIDI_OUT_CH);
          previousPotValues[channel] = value;
        }
      }
    }
  }
}

// ============================================================
// MIDI Input
// ============================================================
void readMIDI() {
  if (!usbDeviceReady()) return;
  while (usb_midi.available()) {
    uint8_t statusByte = usb_midi.read();
    uint8_t dataByte1 = usb_midi.read();
    uint8_t dataByte2 = usb_midi.read();

    unsigned long currentTime = millis();
    if (currentTime - lastMessageTime > batchTimeout) {
      inBatch = true;
      batchMessageCount = 0;
      for (int i = 0; i < NUMPIXELS; i++) keysUpdated[i] = false;
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
        if (velocity != FLASHING_VELOCITY)
          keypadPixels.setPixelColor(padToPixel[padIndex], velocityToColor(velocity));
      }
    } else if (messageType == 0x80) {
      int midiNote = dataByte1;
      int padIndex = noteToPad[midiNote];
      if (padIndex != -1) {
        velocities[midiNote] = 0;
        keysUpdated[padIndex] = true;
        keypadPixels.setPixelColor(padToPixel[padIndex], 0);
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
      if (consecutiveCount >= 4) return true;
    } else {
      consecutiveCount = 0;
    }
  }
  return false;
}

// ============================================================
// LED Functions
// ============================================================
void updateFlashingLEDs() {
  for (int i = 0; i < NUMPIXELS; i++) {
    int midiNote = padToNote[i];
    int velocity = velocities[midiNote];
    if (velocity == FLASHING_VELOCITY) {
      if (flashState) keypadPixels.setPixelColor(padToPixel[i], velocityToColor(velocity));
      else keypadPixels.setPixelColor(padToPixel[i], 0);
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
