#include <Adafruit_TCA8418.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>
#include <MIDI.h>
#include "Adafruit_seesaw.h"
#include <seesaw_neopixel.h>

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
  Serial.begin(115200);  // Initialize Serial for debugging
  Wire.begin();
  Wire.setClock(1000000); // Set I2C clock speed to 400 kHz if supported
  MIDI.begin(MIDI_OUT_CH);
  keypadPixels.begin();

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
      sendRelativeCC(e, delta);
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

#define BASE_CC_NUMBER 20  // Starting CC number for the first potentiometer

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
  switch (velocity) {
    case 0: return keypadPixels.Color(1, 1, 1);      // gray
    case 32: return keypadPixels.Color(48, 12, 0);   // Orange
    case 95: return keypadPixels.Color(0, 32, 0);    // Green (Flashing)
    case 64: return keypadPixels.Color(24, 24, 0);   // yellow (Preview)
    case 127: return keypadPixels.Color(0, 32, 0);   // Green (Selected)
    case 104: return keypadPixels.Color(5, 5, 15);   // Blue
    case 107: return keypadPixels.Color(15, 0, 15);  // Purple
    default: return keypadPixels.Color(0, 0, 0);     // Off
  }
  // switch (velocity) {
  //   case 0: return keypadPixels.Color(0, 0, 0);      // gray
  //   case 32: return keypadPixels.Color(3, 1, 0);   // Orange
  //   case 95: return keypadPixels.Color(0, 3, 0);    // Green (Flashing)
  //   case 64: return keypadPixels.Color(24, 24, 0);   // yellow (Preview)
  //   case 127: return keypadPixels.Color(0, 3, 0);   // Green (Selected)
  //   case 104: return keypadPixels.Color(5, 5, 15);   // Blue
  //   case 107: return keypadPixels.Color(15, 0, 15);  // Purple
  //   default: return keypadPixels.Color(0, 0, 0);     // Off
  // }
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