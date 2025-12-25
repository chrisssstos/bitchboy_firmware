#include <Adafruit_TCA8418.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>
#include <CD74HC4067.h>
#include <MIDI.h>
#include <pio_usb.h>
#include "tusb.h"

// USB Host pin for trackpad (D+ line)
#define HOST_PIN_DP 1

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
    (const char[]){0x09, 0x04},  // 0: Supported language is English
    "BLOCK SYSTEM",                // 1: Manufacturer
    "BitchBoy",                  // 2: Product name
    "123456",                    // 3: Serial number
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
int MIDI_OUT_CH = 1;  // MIDI output channel

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
#define KEY_MOD_LGUI   0x08  // Left GUI (Cmd on Mac)
#define KEY_MOD_LCTRL  0x01  // Left Control
#define KEY_MOD_LSHIFT 0x02  // Left Shift
#define KEY_M          0x10  // M key

// Define the number of rows and columns for the keypad
#define ROWS 8
#define COLS 8
#define NUMPIXELS ROWS* COLS

Adafruit_TCA8418 tca8418;
#define TCA8418_ADDR TCA8418_DEFAULT_ADDR

// New GPIO mappings from second file
CD74HC4067 mux(22, 21, 19, 20);

#define SLIDERS_PIN A0
#define POTS_PIN A1
#define NUM_SLIDERS 12
#define NUM_POTS 8

const int sliderMap[NUM_SLIDERS] = {12, 11, 10, 9, 8, 7, 6, 5, 1, 2, 3, 4};
const int potMap[NUM_POTS] = {2, 4, 6, 8, 1, 3, 5, 7};

// Calibrated values
int sliderMinValues[NUM_SLIDERS] = {375, 369, 396, 374, 385, 366, 380, 358, 377, 375, 362, 375};
int sliderMaxValues[NUM_SLIDERS] = {4095, 4095, 4094, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095};

int potMinValues[NUM_POTS] = {355, 352, 377, 379, 369, 390, 383, 384};
int potMaxValues[NUM_POTS] = {4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095};

int previousSliderValues[NUM_SLIDERS] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
int previousPotValues[NUM_POTS] = {-1, -1, -1, -1, -1, -1, -1, -1};

// Smoothed values for noise reduction
float smoothedSliderValues[NUM_SLIDERS] = {0,0,0,0,0,0,0,0,0,0,0,0};
float smoothedPotValues[NUM_POTS] = {0,0,0,0,0,0,0,0};

// Keypad mapping (your keypad's physical layout)
char keys[ROWS][COLS] = {
  { '0', '1', '2', '3', '4', '5', 'U', 'V' },
  { '6', '7', '8', '9', 'A', 'B', 'X', 'Y' },
  { 'C', 'D', 'E', 'F', 'G', 'H', 'a', 'b' },
  { 'I', 'J', 'K', 'L', 'M', 'N', 'd', 'e' },
  { 'O', 'P', 'Q', 'R', 'S', 'T', 'g', 'h' },
  { 'W', 'X', 'Y', 'Z', '1', '2', '3', '4' },  // New Row 6
  { '5', '6', '7', '8', '9', '0', '-', '=' },  // New Row 7
  { 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i' }   // New Row 8
};


// Map your keypad to Launchpad's MIDI note numbers
int padToNote[NUMPIXELS] = {
  64, 65, 66, 67, 96, 97, 98, 99,  // Row 1
  60, 61, 62, 63, 92, 93, 94, 95,  // Row 2
  56, 57, 58, 59, 88, 89, 90, 91,  // Row 3
  52, 53, 54, 55, 84, 85, 86, 87,  // Row 4
  48, 49, 50, 51, 80, 81, 82, 83,  // Row 5
  44, 45, 46, -1, -1, -1, -1, -1,  // Row 6 (only 3 columns, rest are -1)
  40, 41, 42, -1, -1, -1, -1, -1,  // Row 7
  36, 37, 38, -1, -1, -1, -1, -1   // Row 8
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
int noteToPad[128];           // MIDI notes range from 0 to 127
int velocities[128];          // For flashing LEDs or state tracking
#define FLASHING_VELOCITY 40  // Flashing velocity value

// Variables for batch processing and flashing LEDs
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
Adafruit_NeoPixel keypadPixels(NUMPIXELS, NEO_PIN, NEO_GRB + NEO_KHZ800);  // Renamed from 'pixels' to 'keypadPixels'
unsigned long previousMillis = 0;
const long interval = 500;
bool flashState = false;


void setup() {
  // Set custom USB descriptors before anything else
  TinyUSBDevice.setManufacturerDescriptor("BLOCK SYSTEM");
  TinyUSBDevice.setProductDescriptor("BitchBoy");
  TinyUSBDevice.setSerialDescriptor("123456");
  Serial.begin(115200);  // Initialize Serial for debugging
  Wire.begin();
  Wire.setClock(1000000); // Set I2C clock speed to 1MHz

  // Initialize USB HID for trackpad mouse output
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();

  // Initialize USB HID keyboard for zoom gestures
  usb_keyboard.setReportDescriptor(desc_hid_keyboard, sizeof(desc_hid_keyboard));
  usb_keyboard.begin();

  MIDI.begin(MIDI_OUT_CH);
  keypadPixels.begin();
  keypadPixels.setBrightness(50); // Default brightness

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

void loop() {
  // Handle keypad events
  handleKeypad();

  // Handle incoming MIDI messages
  readMIDI();

  // Handle pots and sliders
  updatePotValues();

  unsigned long currentTime = millis();
  if (inBatch && (currentTime - lastMessageTime > batchTimeout)) {
    inBatch = false;
    if (batchMessageCount >= BATCH_MIN_MESSAGE_COUNT && !shouldSkipRefresh()) {
      // Remove the updateLEDs call since it's not defined
    }
    batchMessageCount = 0;
  }

  if (currentTime - previousMillis >= interval) {
    previousMillis = currentTime;
    flashState = !flashState;
    updateFlashingLEDs();
  }

  keypadPixels.show();
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
        // Send Note On message with velocity 127
        MIDI.sendNoteOn(midiNote, 127, MIDI_OUT_CH);

        // Console log for button press
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
        // Send Note Off message
        MIDI.sendNoteOff(midiNote, 0, MIDI_OUT_CH);

        // Console log for button release
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

  // Snap to endpoints
  if (calibratedValue <= 2) {
    calibratedValue = 0;
  } else if (calibratedValue >= 125) {
    calibratedValue = 127;
  }

  // Uncomment below for verbose debugging of raw ADC values
  // Serial.print("Slider CH");
  // Serial.print(channel);
  // Serial.print(" RAW:");
  // Serial.print(rawValue);
  // Serial.print(" SMOOTHED:");
  // Serial.print((int)smoothedSliderValues[channel]);
  // Serial.print(" CALIBRATED:");
  // Serial.println(calibratedValue);

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

  // Snap to endpoints
  if (calibratedValue <= 2) {
    calibratedValue = 0;
  } else if (calibratedValue >= 125) {
    calibratedValue = 127;
  }

  // Uncomment below for verbose debugging of raw ADC values
  // Serial.print("Pot CH");
  // Serial.print(channel);
  // Serial.print(" RAW:");
  // Serial.print(rawValue);
  // Serial.print(" SMOOTHED:");
  // Serial.print((int)smoothedPotValues[channel]);
  // Serial.print(" CALIBRATED:");
  // Serial.println(calibratedValue);

  return constrain(calibratedValue, 0, 127);
}

void updatePotValues() {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 5) {
    lastUpdate = millis();

    // Handle sliders (CC 0-11)
    for (int sliderNum = 1; sliderNum <= NUM_SLIDERS; sliderNum++) {
      int channel = findChannelForSlider(sliderNum);
      if (channel >= 0) {
        int value = readCalibratedSlider(channel);

        if (abs(value - previousSliderValues[channel]) >= 3) {
          MIDI.sendControlChange(sliderNum - 1, value, MIDI_OUT_CH);
          previousSliderValues[channel] = value;

          // Console log for slider
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

    // Handle pots (CC 20-27)
    for (int potNum = 1; potNum <= NUM_POTS; potNum++) {
      int channel = findChannelForPot(potNum);
      if (channel >= 0) {
        int value = readCalibratedPot(channel);

        if (abs(value - previousPotValues[channel]) >= 3) {
          MIDI.sendControlChange(20 + potNum - 1, value, MIDI_OUT_CH);
          previousPotValues[channel] = value;

          // Console log for pot
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

    batchMessageCount++;  // Increment the message count for this batch

    updateVelocityBuffer(dataByte2);

    uint8_t messageType = statusByte & 0xF0;
    uint8_t channel = statusByte & 0x0F;

    if (messageType == 0x90) {  // Note On message
      int midiNote = dataByte1;
      int velocity = dataByte2;

      int padIndex = noteToPad[midiNote];
      if (padIndex != -1) {
        keysUpdated[padIndex] = true;
        velocities[midiNote] = velocity;
        uint32_t color = velocityToColor(velocity);

        if (velocity == FLASHING_VELOCITY) {
          // Flashing LEDs handled in updateFlashingLEDs
        } else {
          keypadPixels.setPixelColor(padToPixel[padIndex], color);
        }
      }
    } else if (messageType == 0x80) {  // Note Off message
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
        keypadPixels.setPixelColor(padToPixel[i], 0);  // Turn off LED
      }
    }
  }
}

uint32_t velocityToColor(int velocity) {
  // Convert velocity to RGB color based on the provided table
  // Format: case velocity: return keypadPixels.Color(R, G, B);
  // Note: The table values are in hex, we'll convert them to decimal RGB components (0-255)

  switch (velocity) {
    case 0: return keypadPixels.Color(0x06, 0x06, 0x06);    // #BBOY GRAY
    case 1: return keypadPixels.Color(0x1E, 0x1E, 0x1E);    // #1E1E1E
    case 2: return keypadPixels.Color(0x7F, 0x7F, 0x7F);    // #7F7F7F
    case 3: return keypadPixels.Color(0xFF, 0xFF, 0xFF);    // #FFFFFF
    case 4: return keypadPixels.Color(0xFF, 0x4C, 0x4C);    // #FF4C4C
    case 5: return keypadPixels.Color(0xFF, 0x00, 0x00);    // #FF0000
    case 6: return keypadPixels.Color(0x59, 0x00, 0x00);    // #590000
    case 7: return keypadPixels.Color(0x19, 0x00, 0x00);    // #190000
    case 8: return keypadPixels.Color(0xFF, 0xBD, 0x6C);    // #FFBD6C
    case 9: return keypadPixels.Color(0xFF, 0x54, 0x00);    // #FF5400
    case 10: return keypadPixels.Color(0x59, 0x1D, 0x00);   // #591D00
    case 11: return keypadPixels.Color(0x27, 0x1B, 0x00);   // #271B00
    case 12: return keypadPixels.Color(0xFF, 0xFF, 0x4C);   // #FFFF4C
    case 13: return keypadPixels.Color(0xFF, 0xFF, 0x00);   // #FFFF00
    case 14: return keypadPixels.Color(0x59, 0x59, 0x00);   // #595900
    case 15: return keypadPixels.Color(0x19, 0x19, 0x00);   // #191900
    case 16: return keypadPixels.Color(0x88, 0xFF, 0x4C);   // #88FF4C
    case 17: return keypadPixels.Color(0x54, 0xFF, 0x00);   // #54FF00
    case 18: return keypadPixels.Color(0x1D, 0x59, 0x00);   // #1D5900
    case 19: return keypadPixels.Color(0x14, 0x2B, 0x00);   // #142B00
    case 20: return keypadPixels.Color(0x4C, 0xFF, 0x4C);   // #4CFF4C
    case 21: return keypadPixels.Color(0x00, 0xFF, 0x00);   // #00FF00
    case 22: return keypadPixels.Color(0x00, 0x59, 0x00);   // #005900
    case 23: return keypadPixels.Color(0x00, 0x19, 0x00);   // #001900
    case 24: return keypadPixels.Color(0x4C, 0xFF, 0x5E);   // #4CFF5E
    case 25: return keypadPixels.Color(0x00, 0xFF, 0x19);   // #00FF19
    case 26: return keypadPixels.Color(0x00, 0x59, 0x0D);   // #00590D
    case 27: return keypadPixels.Color(0x00, 0x19, 0x02);   // #001902
    case 28: return keypadPixels.Color(0x4C, 0xFF, 0x88);   // #4CFF88
    case 29: return keypadPixels.Color(0x00, 0xFF, 0x55);   // #00FF55
    case 30: return keypadPixels.Color(0x00, 0x59, 0x1D);   // #00591D
    case 31: return keypadPixels.Color(0x00, 0x1F, 0x12);   // #001F12
    case 32: return keypadPixels.Color(0xD6, 0x35, 0x00);   // #BBOY ORANGE
    case 33: return keypadPixels.Color(0x00, 0xFF, 0x99);   // #00FF99
    case 34: return keypadPixels.Color(0x00, 0x59, 0x35);   // #005935
    case 35: return keypadPixels.Color(0x00, 0x19, 0x12);   // #001912
    case 36: return keypadPixels.Color(0x4C, 0xC3, 0xFF);   // #4CC3FF
    case 37: return keypadPixels.Color(0x00, 0xA9, 0xFF);   // #00A9FF
    case 38: return keypadPixels.Color(0x00, 0x41, 0x52);   // #004152
    case 39: return keypadPixels.Color(0x00, 0x10, 0x19);   // #001019
    case 40: return keypadPixels.Color(0x4C, 0x88, 0xFF);   // #4C88FF
    case 41: return keypadPixels.Color(0x00, 0x55, 0xFF);   // #0055FF
    case 42: return keypadPixels.Color(0x00, 0x1D, 0x59);   // #001D59
    case 43: return keypadPixels.Color(0x00, 0x08, 0x19);   // #000819
    case 44: return keypadPixels.Color(0x4C, 0x4C, 0xFF);   // #4C4CFF
    case 45: return keypadPixels.Color(0x00, 0x00, 0xFF);   // #0000FF
    case 46: return keypadPixels.Color(0x00, 0x00, 0x59);   // #000059
    case 47: return keypadPixels.Color(0x00, 0x00, 0x19);   // #000019
    case 48: return keypadPixels.Color(0x87, 0x4C, 0xFF);   // #874CFF
    case 49: return keypadPixels.Color(0x54, 0x00, 0xFF);   // #5400FF
    case 50: return keypadPixels.Color(0x19, 0x00, 0x64);   // #190064
    case 51: return keypadPixels.Color(0x0F, 0x00, 0x30);   // #0F0030
    case 52: return keypadPixels.Color(0xFF, 0x4C, 0xFF);   // #FF4CFF
    case 53: return keypadPixels.Color(0xFF, 0x00, 0xFF);   // #FF00FF
    case 54: return keypadPixels.Color(0x59, 0x00, 0x59);   // #590059
    case 55: return keypadPixels.Color(0x19, 0x00, 0x19);   // #190019
    case 56: return keypadPixels.Color(0xFF, 0x4C, 0x87);   // #FF4C87
    case 57: return keypadPixels.Color(0xFF, 0x00, 0x54);   // #FF0054
    case 58: return keypadPixels.Color(0x59, 0x00, 0x1D);   // #59001D
    case 59: return keypadPixels.Color(0x22, 0x00, 0x13);   // #220013
    case 60: return keypadPixels.Color(0xFF, 0x15, 0x00);   // #FF1500
    case 61: return keypadPixels.Color(0x99, 0x35, 0x00);   // #993500
    case 62: return keypadPixels.Color(0x79, 0x51, 0x00);   // #795100
    case 63: return keypadPixels.Color(0x43, 0x64, 0x00);   // #436400
    case 64: return keypadPixels.Color(0x18, 0x18, 0x00);   // BBOY YELLOW
    case 65: return keypadPixels.Color(0x00, 0x57, 0x35);   // #005735
    case 66: return keypadPixels.Color(0x00, 0x54, 0x7F);   // #00547F
    case 67: return keypadPixels.Color(0x00, 0x00, 0xFF);   // #0000FF
    case 68: return keypadPixels.Color(0x00, 0x45, 0x4F);   // #00454F
    case 69: return keypadPixels.Color(0x25, 0x00, 0xCC);   // #2500CC
    case 70: return keypadPixels.Color(0x7F, 0x7F, 0x7F);   // #7F7F7F
    case 71: return keypadPixels.Color(0x20, 0x20, 0x20);   // #202020
    case 72: return keypadPixels.Color(0xFF, 0x00, 0x00);   // #FF0000
    case 73: return keypadPixels.Color(0xBD, 0xFF, 0x2D);   // #BDFF2D
    case 74: return keypadPixels.Color(0xAF, 0xED, 0x06);   // #AFED06
    case 75: return keypadPixels.Color(0x64, 0xFF, 0x09);   // #64FF09
    case 76: return keypadPixels.Color(0x10, 0x8B, 0x00);   // #108B00
    case 77: return keypadPixels.Color(0x00, 0xFF, 0x87);   // #00FF87
    case 78: return keypadPixels.Color(0x00, 0xA9, 0xFF);   // #00A9FF
    case 79: return keypadPixels.Color(0x00, 0x2A, 0xFF);   // #002AFF
    case 80: return keypadPixels.Color(0x3F, 0x00, 0xFF);   // #3F00FF
    case 81: return keypadPixels.Color(0x7A, 0x00, 0xFF);   // #7A00FF
    case 82: return keypadPixels.Color(0xB2, 0x1A, 0x7D);   // #B21A7D
    case 83: return keypadPixels.Color(0x40, 0x21, 0x00);   // #402100
    case 84: return keypadPixels.Color(0xFF, 0x4A, 0x00);   // #FF4A00
    case 85: return keypadPixels.Color(0x88, 0xE1, 0x06);   // #88E106
    case 86: return keypadPixels.Color(0x72, 0xFF, 0x15);   // #72FF15
    case 87: return keypadPixels.Color(0x00, 0xFF, 0x00);   // #00FF00
    case 88: return keypadPixels.Color(0x3B, 0xFF, 0x26);   // #3BFF26
    case 89: return keypadPixels.Color(0x59, 0xFF, 0x71);   // #59FF71
    case 90: return keypadPixels.Color(0x38, 0xFF, 0xCC);   // #38FFCC
    case 91: return keypadPixels.Color(0x5B, 0x8A, 0xFF);   // #5B8AFF
    case 92: return keypadPixels.Color(0x31, 0x51, 0xC6);   // #3151C6
    case 93: return keypadPixels.Color(0x87, 0x7F, 0xE9);   // #877FE9
    case 94: return keypadPixels.Color(0xD3, 0x1D, 0xFF);   // #D31DFF
    case 95: return keypadPixels.Color(0x00, 0x20, 0x00);   // #BBOY GREEN
    case 96: return keypadPixels.Color(0xFF, 0x7F, 0x00);   // #FF7F00
    case 97: return keypadPixels.Color(0xB9, 0xB0, 0x00);   // #B9B000
    case 98: return keypadPixels.Color(0x90, 0xFF, 0x00);   // #90FF00
    case 99: return keypadPixels.Color(0x83, 0x5D, 0x07);   // #835D07
    case 100: return keypadPixels.Color(0x39, 0x2B, 0x00);  // #392b00
    case 101: return keypadPixels.Color(0x14, 0x4C, 0x10);  // #144C10
    case 102: return keypadPixels.Color(0x0D, 0x50, 0x38);  // #0D5038
    case 103: return keypadPixels.Color(0x15, 0x15, 0x2A);  // #15152A
    case 104: return keypadPixels.Color(0x16, 0x20, 0x5A);  // #16205A
    case 105: return keypadPixels.Color(0x69, 0x3C, 0x1C);  // #693C1C
    case 106: return keypadPixels.Color(0xA8, 0x00, 0x0A);  // #A8000A
    case 107: return keypadPixels.Color(0xDE, 0x51, 0x3D);  // #DE513D
    case 108: return keypadPixels.Color(0xD8, 0x6A, 0x1C);  // #D86A1C
    case 109: return keypadPixels.Color(0xFF, 0xE1, 0x26);  // #FFE126
    case 110: return keypadPixels.Color(0x9E, 0xE1, 0x2F);  // #9EE12F
    case 111: return keypadPixels.Color(0x67, 0xB5, 0x0F);  // #67B50F
    case 112: return keypadPixels.Color(0x1E, 0x1E, 0x30);  // #1E1E30
    case 113: return keypadPixels.Color(0xDC, 0xFF, 0x6B);  // #DCFF6B
    case 114: return keypadPixels.Color(0x80, 0xFF, 0xBD);  // #80FFBD
    case 115: return keypadPixels.Color(0x9A, 0x99, 0xFF); // #9A99FF
    case 116: return keypadPixels.Color(0x8E, 0x66, 0xFF); // #8E66FF
    case 117: return keypadPixels.Color(0x40, 0x40, 0x40); // #404040
    case 118: return keypadPixels.Color(0x75, 0x75, 0x75);  // #757575
    case 119: return keypadPixels.Color(0xE0, 0xFF, 0xFF);  // #E0FFFF
    case 120: return keypadPixels.Color(0xA0, 0x00, 0x00);  // #A00000
    case 121: return keypadPixels.Color(0x35, 0x00, 0x00);  // #350000
    case 122: return keypadPixels.Color(0x1A, 0xD0, 0x00);  // #1AD000
    case 123: return keypadPixels.Color(0x07, 0x42, 0x00);  // #074200
    case 124: return keypadPixels.Color(0xB9, 0xB0, 0x00);  // #B9B000
    case 125: return keypadPixels.Color(0x3F, 0x31, 0x00);  // #3F3100
    case 126: return keypadPixels.Color(0xB3, 0x5F, 0x00);  // #B35F00
    case 127: return keypadPixels.Color(0x00, 0x20, 0x00);  // #BBOY GREEN

    default: return keypadPixels.Color(0x1E, 0x1E, 0x1E);   // Off (default)
  }
}

// ============================================================================
// Core 1: USB Host for Trackpad
// ============================================================================

void setup1() {
  delay(1000);  // Wait for Core 0 to initialize
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = HOST_PIN_DP;
  pio_cfg.pinout = PIO_USB_PINOUT_DMDP;
  USBHost.configure_pio_usb(1, &pio_cfg);
  USBHost.begin(1);
  Serial.println("USB Host ready");
}

void loop1() {
  USBHost.task();
}

// TinyUSB Host HID callbacks
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
  Serial.println("Trackpad connected");
  tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_REPORT);
  tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  Serial.println("Trackpad disconnected");
}

// Send pinch key shortcut for both Mac and Windows
void sendPinchKey() {
  uint8_t keyReport[8] = {0, 0, 0, 0, 0, 0, 0, 0};

  // First send Ctrl+Shift+M (for Windows)
  keyReport[0] = KEY_MOD_LCTRL | KEY_MOD_LSHIFT;  // Modifier: Ctrl + Shift
  keyReport[2] = KEY_M;
  usb_keyboard.sendReport(0, keyReport, 8);

  delay(10);  // Small delay for key press

  // Release all keys
  memset(keyReport, 0, 8);
  usb_keyboard.sendReport(0, keyReport, 8);

  delay(10);  // Small delay between shortcuts

  // Then send Cmd+Shift+M (for Mac)
  keyReport[0] = KEY_MOD_LGUI | KEY_MOD_LSHIFT;  // Modifier: Cmd + Shift
  keyReport[2] = KEY_M;
  usb_keyboard.sendReport(0, keyReport, 8);

  delay(10);  // Small delay for key press

  // Release all keys
  memset(keyReport, 0, 8);
  usb_keyboard.sendReport(0, keyReport, 8);
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  if (!usb_hid.ready()) {
    tuh_hid_receive_report(dev_addr, instance);
    return;
  }

  uint8_t report_out[5] = {0, 0, 0, 0, 0};  // buttons, x, y, wheel, pan

  if (len == 6 && report[0] == 0x06) {
    // Normal movement + scroll
    // Format: [ID=06] [buttons] [X] [Y] [V-scroll] [H-scroll]
    report_out[0] = report[1];           // buttons
    report_out[1] = report[2];           // X
    report_out[2] = report[3];           // Y
    report_out[3] = (int8_t)report[4];   // vertical scroll
    report_out[4] = (int8_t)report[5];   // horizontal scroll (pan)

    usb_hid.sendReport(0, report_out, 5);
  }
  else if (len == 4 && report[0] == 0x08) {
    // Pinch gesture - send Cmd+Shift+M for either direction
    // Format: [ID=08] [flags] [unused] [gesture_type]
    // flags: 0x08 = gesture active, 0x00 = idle
    // gesture_type: 0x56 = pinch out, 0x57 = pinch in
    static unsigned long lastPinchTime = 0;
    const unsigned long PINCH_RATE_LIMIT_MS = 500;  // Only send every 500ms

    uint8_t flags = report[1];
    uint8_t gestureType = report[3];

    if (flags & 0x08) {
      unsigned long now = millis();
      if (now - lastPinchTime >= PINCH_RATE_LIMIT_MS) {
        if (gestureType == 0x56 || gestureType == 0x57) {
          sendPinchKey();  // Ctrl+Shift+M (Windows) + Cmd+Shift+M (Mac)
          Serial.println("Pinch -> Ctrl+Shift+M (Win) / Cmd+Shift+M (Mac)");
          lastPinchTime = now;
        }
      }
    }
  }

  tuh_hid_receive_report(dev_addr, instance);
}
