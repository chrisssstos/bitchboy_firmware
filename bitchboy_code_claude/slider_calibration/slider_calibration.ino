// Slider calibration sketch for BitchBoy
//
// Reads all 12 slider channels via the CD74HC4067 mux on A0 and prints
// raw 12-bit ADC values. Use the Serial Monitor at 115200 baud.
//
// Procedure:
//   1. Flash this sketch.
//   2. Open the Serial Monitor (115200, line ending = Newline).
//   3. Move ALL sliders to their minimum position. Send 'n' to capture mins.
//   4. Move ALL sliders to their maximum position. Send 'x' to capture maxes.
//   5. Send 'p' to print the final sliderMinValues / sliderMaxValues arrays
//      ready to paste into bitchboy_main.ino.
//
// Other commands:
//   'l' -> live stream of all 12 channels
//   'r' -> reset captured min/max
//   'h' -> help
//
// Pin / mapping match bitchboy_main.ino:
//   mux pins (S0..S3) = 22, 21, 19, 20
//   SLIDERS_PIN       = A0
//   sliderMap[ch]     = physical slider label printed on the panel

#include <Adafruit_TinyUSB.h>  // required when Tools > USB Stack = Adafruit TinyUSB
#include <CD74HC4067.h>

#define SLIDERS_PIN A0
#define NUM_SLIDERS 12
#define SAMPLES_PER_READ 10

CD74HC4067 mux(22, 21, 19, 20);

// Mux channel index -> physical slider number on the panel.
// Same mapping that bitchboy_main.ino uses.
const int sliderMap[NUM_SLIDERS] = {12, 11, 10, 9, 8, 7, 6, 5, 1, 2, 3, 4};

int capturedMin[NUM_SLIDERS];
int capturedMax[NUM_SLIDERS];
bool minCaptured = false;
bool maxCaptured = false;

int readChannelRaw(int channel) {
  mux.channel(channel);
  delayMicroseconds(100);
  long sum = 0;
  for (int i = 0; i < SAMPLES_PER_READ; i++) {
    sum += analogRead(SLIDERS_PIN);
    delayMicroseconds(10);
  }
  return (int)(sum / SAMPLES_PER_READ);
}

void readAll(int *out) {
  for (int ch = 0; ch < NUM_SLIDERS; ch++) {
    out[ch] = readChannelRaw(ch);
  }
}

void printLiveOnce() {
  int v[NUM_SLIDERS];
  readAll(v);
  Serial.print("LIVE  ");
  for (int ch = 0; ch < NUM_SLIDERS; ch++) {
    Serial.print("ch");
    Serial.print(ch);
    Serial.print("(s");
    Serial.print(sliderMap[ch]);
    Serial.print(")=");
    Serial.print(v[ch]);
    if (ch < NUM_SLIDERS - 1) Serial.print("  ");
  }
  Serial.println();
}

void captureMin() {
  readAll(capturedMin);
  minCaptured = true;
  Serial.println();
  Serial.println("=== MIN captured ===");
  for (int ch = 0; ch < NUM_SLIDERS; ch++) {
    Serial.print("  ch");
    Serial.print(ch);
    Serial.print(" (slider #");
    Serial.print(sliderMap[ch]);
    Serial.print(") min = ");
    Serial.println(capturedMin[ch]);
  }
}

void captureMax() {
  readAll(capturedMax);
  maxCaptured = true;
  Serial.println();
  Serial.println("=== MAX captured ===");
  for (int ch = 0; ch < NUM_SLIDERS; ch++) {
    Serial.print("  ch");
    Serial.print(ch);
    Serial.print(" (slider #");
    Serial.print(sliderMap[ch]);
    Serial.print(") max = ");
    Serial.println(capturedMax[ch]);
  }
}

void printArrays() {
  Serial.println();
  Serial.println("=== Paste into bitchboy_main.ino ===");

  if (!minCaptured) {
    Serial.println("// WARNING: min not captured yet");
  }
  Serial.print("int sliderMinValues[NUM_SLIDERS] = {");
  for (int ch = 0; ch < NUM_SLIDERS; ch++) {
    Serial.print(minCaptured ? capturedMin[ch] : 0);
    if (ch < NUM_SLIDERS - 1) Serial.print(", ");
  }
  Serial.println("};");

  if (!maxCaptured) {
    Serial.println("// WARNING: max not captured yet");
  }
  Serial.print("int sliderMaxValues[NUM_SLIDERS] = {");
  for (int ch = 0; ch < NUM_SLIDERS; ch++) {
    Serial.print(maxCaptured ? capturedMax[ch] : 4095);
    if (ch < NUM_SLIDERS - 1) Serial.print(", ");
  }
  Serial.println("};");
  Serial.println();

  if (minCaptured && maxCaptured) {
    Serial.println("Sanity check (max - min per channel):");
    for (int ch = 0; ch < NUM_SLIDERS; ch++) {
      int span = capturedMax[ch] - capturedMin[ch];
      Serial.print("  ch");
      Serial.print(ch);
      Serial.print(" (slider #");
      Serial.print(sliderMap[ch]);
      Serial.print(") span = ");
      Serial.print(span);
      if (span < 1000) Serial.print("   <-- LOW, check wiring");
      Serial.println();
    }
  }
}

void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  n  capture MIN values  (sliders all the way DOWN first)");
  Serial.println("  x  capture MAX values  (sliders all the way UP first)");
  Serial.println("  p  print final arrays for bitchboy_main.ino");
  Serial.println("  l  live stream all 12 channels (one shot)");
  Serial.println("  r  reset captured min/max");
  Serial.println("  h  help");
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  analogReadResolution(12);

  for (int ch = 0; ch < NUM_SLIDERS; ch++) {
    capturedMin[ch] = 0;
    capturedMax[ch] = 4095;
  }

  Serial.println();
  Serial.println("BitchBoy slider calibration");
  Serial.println("12-bit ADC (0-4095), 12 mux channels on A0");
  printHelp();
}

void loop() {
  // Always stream live values so you can watch what each slider reads.
  printLiveOnce();
  delay(150);

  // Drain serial; one-character commands.
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r' || c == ' ') continue;
    switch (c) {
      case 'n': case 'N': captureMin(); break;
      case 'x': case 'X': captureMax(); break;
      case 'p': case 'P': printArrays(); break;
      case 'l': case 'L': printLiveOnce(); break;
      case 'r': case 'R':
        minCaptured = false;
        maxCaptured = false;
        Serial.println("Reset.");
        break;
      case 'h': case 'H': case '?':
        printHelp();
        break;
      default:
        Serial.print("Unknown command: ");
        Serial.println(c);
        printHelp();
        break;
    }
  }
}
