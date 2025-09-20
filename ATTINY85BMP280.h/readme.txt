ATTINY85BMP280

A standalone Arduino library for the BMP280 barometric pressure sensor, designed specifically for the ATtiny85 (and similar USI-based AVRs).
Implements its own lightweight I²C routines (no Wire / TinyWireM needed) and full Bosch calibration math for accurate relative altitude.

✨ Features

No dependencies – works on bare ATtiny85 (bit-banged I²C on PB0 = SDA, PB2 = SCL).

Full Bosch compensation – matches Adafruit BMP280 readings within ~±2 m at ~900 m ΔP.

Relative altitude – correctly increases when pressure drops.

Baseline zeroing – average ground pressure and use as reference.

Launch ready – includes warm-up discard of first 3 samples.

Fast mode – optional configureFast() sets oversampling/filter for rocket use.

Low footprint – optimized for 8 kB flash ATtiny85.

📌 Wiring (ATtiny85 → BMP280)
ATtiny85 Pin	Function	BMP280 Pin
PB0 (pin 5)	SDA	SDA
PB2 (pin 7)	SCL	SCL
VCC (3.3 V)	Power	VCC
GND (pin 4)	Ground	GND

⚠️ BMP280 must be 3.3 V. Use pull-ups (~4.7 kΩ–10 kΩ) from SDA/SCL to 3.3 V if your module doesn’t have them.

🛠 API Reference
bool begin(uint8_t addr = 0x76);
void measure();
float getPressurePa();
float getTemperatureC();         // optional
void setBaselinePressure(float p);
float getRelativeAltitudeM();
void configureFast();            // optional: set x4 press oversample, filter x2

🚀 Example: Altitude to Serial
#include <ATTINY85BMP280.h>
#include <SoftwareSerial.h>

ATTINY85BMP280 bmp;
SoftwareSerial mySerial(3, 4); // RX, TX pins

void setup() {
  mySerial.begin(115200);
  bmp.begin(0x76);

  // discard first few samples
  for (int i = 0; i < 3; i++) { bmp.measure(); delay(20); }

  // average baseline
  float sum = 0;
  for (int i = 0; i < 20; i++) {
    bmp.measure();
    sum += bmp.getPressurePa();
    delay(20);
  }
  bmp.setBaselinePressure(sum / 20.0f);

  // optional: configure for rocket use
  bmp.configureFast();

  mySerial.println("BMP280 ready...");
}

void loop() {
  bmp.measure();
  float alt = bmp.getRelativeAltitudeM();
  mySerial.println(alt, 2);   // numeric only, 2 decimals
  delay(50); // ~20 Hz
}

📊 Accuracy Notes

Matches Adafruit BMP280 within ~±2 m at ~900 m altitude changes.

Baseline noise ~±0.5 m (RP2040 with floats gets ~±0.2 m).

Discarding first 2–3 samples avoids the 100–300 m startup glitch.

For rockets: use configureFast() for faster response, or default config for smoother readings.
