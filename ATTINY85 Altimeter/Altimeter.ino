#include <ATTINY85BMP280.h>

// ==== LED pins (ATtiny85 Arduino numbering) ====
#define LED_ONES     1   // PB1 = physical pin 6
#define LED_TENS     3   // PB3 = physical pin 2
#define LED_HUNDREDS 4   // PB4 = physical pin 3

ATTINY85BMP280 bmp;

float baselineAlt = 0.0;
float maxAlt = 0.0;

enum State { PRELAUNCH, FLIGHT, POSTFLIGHT };
State state = PRELAUNCH;

unsigned long launchTime = 0;
unsigned long lastFlashTime = 0;
const unsigned long flightDuration = 6000;   // 6 s logging window
const unsigned long flashInterval = 10000;  // flash max every 10 s after flight

// ====== LED helpers ======
void flashDigit(int pin, int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(pin, HIGH);
    delay(200);
    digitalWrite(pin, LOW);
    delay(200);
  }
  delay(500); // pause between digits
}

void flashAltitude(int altitude) {
  int hundreds = (altitude / 100) % 10;
  int tens     = (altitude / 10) % 10;
  int ones     = altitude % 10;

  if (hundreds > 0) flashDigit(LED_HUNDREDS, hundreds);
  if (tens > 0 || hundreds > 0) flashDigit(LED_TENS, tens);
  flashDigit(LED_ONES, ones);
}

// ====== Setup ======
void setup() {
  pinMode(LED_ONES, OUTPUT);
  pinMode(LED_TENS, OUTPUT);
  pinMode(LED_HUNDREDS, OUTPUT);

  bmp.begin();

  // Baseline averaging
  float sum = 0.0;
  for (int i = 0; i < 20; i++) {
    bmp.measure();
    sum += bmp.getAltitudeM();
    delay(50);
  }
  baselineAlt = sum / 20.0;
  maxAlt = 0.0;
}

// ====== Loop ======
void loop() {
  bmp.measure();
  float alt = bmp.getAltitudeM() - baselineAlt;
  if (alt < 0) alt = 0; // clamp

  switch (state) {
    case PRELAUNCH:
      // Heartbeat on ones LED every 2s
      if (millis() % 2000 < 100) digitalWrite(LED_ONES, HIGH);
      else digitalWrite(LED_ONES, LOW);

      if (alt > 5.0) {
        state = FLIGHT;
        launchTime = millis();
      }
      break;

    case FLIGHT:
      if (alt > maxAlt) maxAlt = alt;

      if (millis() - launchTime > flightDuration) {
        state = POSTFLIGHT;
        digitalWrite(LED_ONES, LOW);
        digitalWrite(LED_TENS, LOW);
        digitalWrite(LED_HUNDREDS, LOW);
        lastFlashTime = millis();
      }
      break;

    case POSTFLIGHT:
      if (millis() - lastFlashTime >= flashInterval) {
        flashAltitude((int)maxAlt);
        lastFlashTime = millis();
      }
      break;
  }
}
