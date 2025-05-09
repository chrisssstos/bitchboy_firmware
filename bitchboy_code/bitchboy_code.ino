#include <Adafruit_TCA8418.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>
#include <MIDI.h>
#include "Adafruit_seesaw.h"
#include <seesaw_neopixel.h>
#include "tusb.h"

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
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

// Define the number of rows and columns for the keypad
#define ROWS 8
#define COLS 8
#define NUMPIXELS ROWS* COLS

Adafruit_TCA8418 tca8418;
#define TCA8418_ADDR TCA8418_DEFAULT_ADDR

// Define Seesaw addresses for two boards
#define SEESAW_ADDR_1 0x49  // Address for the first board
#define SEESAW_ADDR_2 0x4A  // Address for the second board (after cutting A0)

// Define encoder switch pins (these are the same for both boards)
#define SS_ENC0_SWITCH 12
#define SS_ENC1_SWITCH 14
#define SS_ENC2_SWITCH 17
#define SS_ENC3_SWITCH 9

#define MUX_S0 0
#define MUX_S1 1
#define MUX_S2 2
#define MUX_S3 3
#define MUX_ANALOG_PIN 26  // GP26 (ADC0)
#define NUM_POTS 12
#define SMOOTHING_FACTOR 0.2
#define BASE_CC_NUMBER 20  // Starting CC number for the first potentiometer

// New brightness control
uint8_t led_brightness = 65; // Default brightness value (0-255)
bool button83Pressed = false;
bool button44Pressed = false;

int potValues[NUM_POTS]; 

Adafruit_seesaw ss1 = Adafruit_seesaw(&Wire);  // First board
Adafruit_seesaw ss2 = Adafruit_seesaw(&Wire);  // Second board

int32_t enc_positions_1[4] = { 0, 0, 0, 0 };  // Encoder positions for board 1
int32_t enc_positions_2[4] = { 0, 0, 0, 0 };  // Encoder positions for board 2

// NeoPixel setup for the encoder
#define SS_NEO_PIN 18
seesaw_NeoPixel encoderPixels = seesaw_NeoPixel(4, SS_NEO_PIN, NEO_GRB + NEO_KHZ800);  // Renamed for clarity
bool previousState[8] = {false, false, false, false, false, false, false, false};

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
  0, 1, 2, 3, 4, 5, 6, 7,          // Row 1
  15, 14, 13, 12, 11, 10, 9, 8,    // Row 2
  16, 17, 18, 19, 20, 21, 22, 23,  // Row 3
  31, 30, 29, 28, 27, 26, 25, 24,  // Row 4
  32, 33, 34, 35, 36, 37, 38, 39,  // Row 5
  40, 41, 42, -1, -1, -1, -1, -1,  // Row 6
  45, 44, 43, -1, -1, -1, -1, -1,  // Row 7
  46, 47, 48, -1, -1, -1, -1, -1   // Row 8
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

// Arrays for the switch pin numbers on both boards
const int encoderSwitchPins[] = { SS_ENC0_SWITCH, SS_ENC1_SWITCH, SS_ENC2_SWITCH, SS_ENC3_SWITCH };

// Array of references to the Seesaw boards
Adafruit_seesaw* seesawBoards[] = { &ss1, &ss2 };
// Define the MIDI note range for encoder presses (e.g., starting from note 120)
int encoderToNote[8] = { 120, 121, 122, 123, 124, 125, 126, 127 };


void setup() {
  // Set custom USB descriptors before anything else
  TinyUSBDevice.setManufacturerDescriptor("BLOCK SYSTEM");
  TinyUSBDevice.setProductDescriptor("BitchBoy");
  TinyUSBDevice.setSerialDescriptor("123456");
  Serial.begin(115200);  // Initialize Serial for debugging
  Wire.begin();
  Wire.setClock(1000000); // Set I2C clock speed to 400 kHz if supported
  MIDI.begin(MIDI_OUT_CH);
  keypadPixels.begin();
  keypadPixels.setBrightness(led_brightness); // Initialize with default brightness

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
  
  // Initialize the first board
  if (!ss1.begin(SEESAW_ADDR_1)) {
    Serial.println("Couldn't find Seesaw board 1");
    while (1) delay(10);
  }

  // Initialize the second board
  if (!ss2.begin(SEESAW_ADDR_2)) {
    Serial.println("Couldn't find Seesaw board 2");
    while (1) delay(10);
  }

  // Set the pin modes for the encoder switches on the first board
  ss1.pinMode(SS_ENC0_SWITCH, INPUT_PULLUP);
  ss1.pinMode(SS_ENC1_SWITCH, INPUT_PULLUP);
  ss1.pinMode(SS_ENC2_SWITCH, INPUT_PULLUP);
  ss1.pinMode(SS_ENC3_SWITCH, INPUT_PULLUP);

  // Set the pin modes for the encoder switches on the second board
  ss2.pinMode(SS_ENC0_SWITCH, INPUT_PULLUP);
  ss2.pinMode(SS_ENC1_SWITCH, INPUT_PULLUP);
  ss2.pinMode(SS_ENC2_SWITCH, INPUT_PULLUP);
  ss2.pinMode(SS_ENC3_SWITCH, INPUT_PULLUP);

  // Enable interrupts for the encoders on both boards
  ss1.setGPIOInterrupts(1UL << SS_ENC0_SWITCH | 1UL << SS_ENC1_SWITCH | 1UL << SS_ENC2_SWITCH | 1UL << SS_ENC3_SWITCH, 1);
  ss2.setGPIOInterrupts(1UL << SS_ENC0_SWITCH | 1UL << SS_ENC1_SWITCH | 1UL << SS_ENC2_SWITCH | 1UL << SS_ENC3_SWITCH, 1);

  for (int e = 0; e < 4; e++) {
    ss1.enableEncoderInterrupt(e);  // For the first board
    ss2.enableEncoderInterrupt(e);  // For the second board
  }

  Serial.println("Both encoder boards initialized.");

  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  pinMode(MUX_S3, OUTPUT);
}

void loop() {
  // Handle keypad events
  handleKeypad();

  // Handle incoming MIDI messages
  readMIDI();

  // Handle rotary encoder input
  handleRotaryEncoders();
  checkEncoderSwitches();

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
      
      // Check for special key combinations for brightness control
      if (midiNote == 83) {
        button83Pressed = pressed;
        
      Serial.print("83 PRESSED");
  
      } else if (midiNote == 44) {
        button44Pressed = pressed;
      }
      
      if (pressed) {
        // Send Note On message with velocity 127
        MIDI.sendNoteOn(midiNote, 127, MIDI_OUT_CH);
      } else {
        // Send Note Off message
        MIDI.sendNoteOff(midiNote, 0, MIDI_OUT_CH);
      }
    }
  }
}

void checkEncoderSwitches() {
  // Loop through each board
  for (int board = 0; board < 2; board++) {
    // Loop through each encoder switch
    for (int enc = 0; enc < 4; enc++) {
      bool pressed = !seesawBoards[board]->digitalRead(encoderSwitchPins[enc]);
      int encoderIndex = (board * 4) + enc;  // Unique index for the encoders across both boards
      int midiNote = encoderToNote[encoderIndex];  // Get the corresponding MIDI note

      // Check if the state has changed from the previous state
      if (pressed != previousState[encoderIndex]) {
        if (pressed) {
          // Send Note On message with velocity 127 when pressed
          MIDI.sendNoteOn(midiNote, 127, MIDI_OUT_CH);
          Serial.print("ENC");
          Serial.print(enc);
          Serial.print(" on Board ");
          Serial.print(board + 1);
          Serial.println(" pressed! MIDI Note On sent.");
        } else {
          // Send Note Off message when released
          MIDI.sendNoteOff(midiNote, 0, MIDI_OUT_CH);
          Serial.print("ENC");
          Serial.print(enc);
          Serial.print(" on Board ");
          Serial.print(board + 1);
          Serial.println(" released! MIDI Note Off sent.");
        }
        // Update the previous state for this encoder
        previousState[encoderIndex] = pressed;
      }
    }
  }
}

void handleRotaryEncoders() {
  for (int e = 0; e < 4; e++) {
    int32_t new_enc_position = ss1.getEncoderPosition(e);
    int32_t delta = new_enc_position - enc_positions_1[e];  // Calculate the delta
    

    if (delta != 0) {
      enc_positions_1[e] = new_enc_position;
      
      // Special case for encoder 3 (index 3) on board 1 for brightness control
      if (e == 3 && button83Pressed && button44Pressed) {
        // Adjust brightness instead of sending MIDI CC
        adjustBrightness(delta);
      } else {
        sendRelativeCC(e, delta);
      }
      
      Serial.print("Board 1 Encoder #");
      Serial.print(e);
      Serial.print(" -> Delta: ");
      Serial.println(delta);
    }

    // Acknowledge the interrupt by reading the pin
    ss1.digitalRead(SS_ENC0_SWITCH + e); // Acknowledge the interrupt by reading each pin
  }

  for (int e = 0; e < 4; e++) {
    int32_t new_enc_position = ss2.getEncoderPosition(e);
    int32_t delta = new_enc_position - enc_positions_2[e];  // Calculate the delta
    // delta = -delta;  // Reverse the direction by inverting delta
    if (delta != 0) {
      enc_positions_2[e] = new_enc_position;
      sendRelativeCC(e + 4, delta);  // Offset for second board
      Serial.print("Board 2 Encoder #");
      Serial.print(e);
      Serial.print(" -> Delta: ");
      Serial.println(delta);
    }

    // Acknowledge the interrupt by reading the pin
    ss2.digitalRead(SS_ENC0_SWITCH + e); // Acknowledge the interrupt by reading each pin
  }
}

// New function to adjust LED brightness
void adjustBrightness(int32_t delta) {
  // Save current brightness for comparison
  uint8_t old_brightness = led_brightness;
  
  if (delta > 0) {
    // Increase brightness, but cap at 255
    led_brightness = min(255, led_brightness + 8);
  } else if (delta < 0) {
    // Decrease brightness, but don't go below 10 (not 1 to prevent blackout)
    led_brightness = max(10, led_brightness - 8);
  }
  
  // Only update if brightness actually changed
  if (led_brightness != old_brightness) {
    keypadPixels.setBrightness(led_brightness);
    
    // Force a complete redraw of all LEDs to maintain color accuracy
    for (int i = 0; i < NUMPIXELS; i++) {
      int midiNote = padToNote[i];
      if (midiNote != -1) {
        uint32_t color = velocityToColor(velocities[midiNote]);
        keypadPixels.setPixelColor(padToPixel[i], color);
      }
    }
    keypadPixels.show();
    
    Serial.print("LED Brightness adjusted to: ");
    Serial.println(led_brightness);
  }
}

void updatePotValues() {
  for (int i = 0; i < NUM_POTS; i++) {
    int rawValue = readMuxChannel(i);

    // Apply a low-pass filter
    int filteredValue = potValues[i] * (1 - SMOOTHING_FACTOR) + rawValue * SMOOTHING_FACTOR;

    // Constrain filteredValue to stay within 0 to 1023
    filteredValue = constrain(filteredValue, 0, 1023);
  
    // Only update and send MIDI if the value has changed significantly
    if (abs(filteredValue - potValues[i]) > 4) {
      potValues[i] = filteredValue;
      
      // Map filteredValue to MIDI CC range (0-127) and hard limit it
      int ccValue = map(filteredValue, 510, 980, 0, 127);
      // int ccValue = map(rawValue, 460, 1023, 0, 127);
      ccValue = constrain(ccValue, 0, 127);  // Hard limit to 0-127
      
      // Send MIDI CC message with a unique CC number for each potentiometer
      MIDI.sendControlChange(BASE_CC_NUMBER + i, ccValue, MIDI_OUT_CH);

      // Debugging output to Serial Monitor
      Serial.print("Potentiometer ");
      Serial.print(i);
      Serial.print(" RAW value: ");
      Serial.print(rawValue);
      Serial.print(" FILT value: ");
      Serial.print(filteredValue);
      Serial.print(" mapped to CC: ");
      Serial.print(BASE_CC_NUMBER + i);
      Serial.print(" with value: ");
      Serial.println(ccValue);
    }
  }
}

void sendRelativeCC(int encoderIndex, int32_t delta) {
  uint8_t relativeValue = 0;

  if (delta > 0) {
    relativeValue = 1;  // Small increment
  } else if (delta < 0) {
    relativeValue = 127;  // Small decrement
  }

  // Send relative MIDI CC message for the given encoder
  MIDI.sendControlChange(encoderIndex, relativeValue, MIDI_OUT_CH);
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
    case 0: return keypadPixels.Color(0x04, 0x04, 0x04);    // #BBOY GRAY
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

void selectMuxChannel(uint8_t channel) {
  digitalWrite(MUX_S0, channel & 0x01);
  digitalWrite(MUX_S1, (channel >> 1) & 0x01);
  digitalWrite(MUX_S2, (channel >> 2) & 0x01);
  digitalWrite(MUX_S3, (channel >> 3) & 0x01);
}

int readMuxChannel(uint8_t channel) {
  selectMuxChannel(channel);                // Set multiplexer to the specified channel
  delayMicroseconds(10);                    // Short delay for stability
  return analogRead(MUX_ANALOG_PIN);        // Read the analog value
}