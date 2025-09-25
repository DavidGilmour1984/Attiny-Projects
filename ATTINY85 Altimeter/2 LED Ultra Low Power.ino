/*
  ==============================================================
   ATtiny85 + BMP280 Rocket Altitude Logger (Interrupt + Deep Sleep)
  ==============================================================

   Behaviour:
   - Starts in deep sleep (PWR_DOWN)
   - Wakes from sleep on PB1 (SWITCH_PIN) interrupt
   - On waking:
       • Long flash once both LEDs
       • Takes 20 pressure samples → sets baseline
   - Detects launch (>5 m rise), logs altitude at 40 Hz for 6 s
   - Stores maximum altitude
   - After flight, altitude displayed with LEDs (place+value scheme)
   - At any time, holding switch ≥6 s:
       • Triple long flash both LEDs
       • Goes back into deep sleep
   - Short press (<6 s) in landed state:
       • Prints altitude
*/

#include "ATTINY85BMP280.h"
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>

ATTINY85BMP280 bmp;

// ==== Pin definitions ====
#define LED_VALUE    3   // PB3
#define LED_PLACE    4   // PB4
#define SWITCH_PIN   1   // PB1 (PCINT1)

// ==== Timing ====
#define FLASH_ON_TIME    20
#define FLASH_OFF_TIME   800

// ==== State tracking ====
volatile bool wakeFlag = false;
bool launched = false;
unsigned long launchTime = 0;
float maxAltitude = 0;

// ==== Interrupt on switch ====
ISR(PCINT0_vect) {
  wakeFlag = true;  // Triggered when PB1 changes
}

// ---------------------------------------------------
// Deep sleep until switch interrupt
void goToSleep() {
  // triple long flash before sleep
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_VALUE, HIGH);
    digitalWrite(LED_PLACE, HIGH);
    delay(600);
    digitalWrite(LED_VALUE, LOW);
    digitalWrite(LED_PLACE, LOW);
    delay(400);
  }

  // enable pin change interrupt on PB1
  GIMSK |= (1 << PCIE);    // enable pin change interrupt
  PCMSK |= (1 << PCINT1);  // enable PB1 interrupt

  wakeFlag = false;
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu();

  // wakes here
  sleep_disable();
  GIMSK &= ~(1 << PCIE);   // disable pin change interrupt
}

// ---------------------------------------------------
// Zeroing: average 20 pressure readings
void zeroAltitude() {
  long sum = 0;
  for (int i = 0; i < 20; i++) {
    bmp.measure();
    sum += bmp.getPressurePa();
    delay(50);
  }
  bmp.setBaselinePressure(sum / 20);
}

// ---------------------------------------------------
// Flash helper (normal digits 1–9)
void flashLED(int pin, int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(pin, HIGH);
    delay(FLASH_ON_TIME);
    digitalWrite(pin, LOW);
    delay(FLASH_OFF_TIME);
  }
}

// ---------------------------------------------------
// Flash a single digit (0–9) on VALUE LED
void flashDigitValue(int digit) {
  if (digit == 0) {
    // Zero shown as one long flash
    digitalWrite(LED_VALUE, HIGH);
    delay(600);
    digitalWrite(LED_VALUE, LOW);
    delay(FLASH_OFF_TIME);
  } else {
    flashLED(LED_VALUE, digit);
  }
}

// ---------------------------------------------------
// Output altitude as PLACE flashes first, then VALUE
void flashAltitudePlaceValue(long altitude) {
  int thousands = altitude / 1000;
  int hundreds  = (altitude / 100) % 10;
  int tens      = (altitude / 10) % 10;
  int ones      = altitude % 10;

  if (thousands > 0) {
    flashLED(LED_PLACE, 4);
    flashDigitValue(thousands);
  }
  if (hundreds > 0 || thousands > 0) {
    flashLED(LED_PLACE, 3);
    flashDigitValue(hundreds);
  }
  if (tens > 0 || hundreds > 0 || thousands > 0) {
    flashLED(LED_PLACE, 2);
    flashDigitValue(tens);
  }
  flashLED(LED_PLACE, 1);
  flashDigitValue(ones);
}

// ---------------------------------------------------
void setup() {
  pinMode(LED_VALUE, OUTPUT);
  pinMode(LED_PLACE, OUTPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP);

  bmp.begin();

  // enter sleep immediately at power-up
  goToSleep();
}

// ---------------------------------------------------
void loop() {
  // wait until switch wakes us
  if (!wakeFlag) {
    goToSleep();
    return;
  }
  wakeFlag = false;

  // === Wakeup flash immediately ===
  digitalWrite(LED_VALUE, HIGH);
  digitalWrite(LED_PLACE, HIGH);
  delay(600);
  digitalWrite(LED_VALUE, LOW);
  digitalWrite(LED_PLACE, LOW);
  delay(200);

  // === Then zero altitude ===
  zeroAltitude();
  launched = false;
  launchTime = 0;
  maxAltitude = 0;

  // ==== Main run loop ====
  while (true) {
    // --- Check for short vs. long press ---
    if (digitalRead(SWITCH_PIN) == LOW) {
      unsigned long t0 = millis();
      while (digitalRead(SWITCH_PIN) == LOW) {
        if (millis() - t0 >= 6000) {
          // === Long hold (≥6s) → shutdown ===
          goToSleep();
          return;
        }
      }
      // === Short press (<6s) in LANDED → altitude ===
      if (launched && millis() - launchTime > 6000) {
        flashAltitudePlaceValue((long)maxAltitude);
      }
    }

    // ---- Pre-launch ----
    if (!launched) {
      bmp.measure();
      float alt = bmp.getRelativeAltitudeM();

      if (alt > 5) {
        launched = true;
        launchTime = millis();
        maxAltitude = alt;
      }
      delay(500);
      continue;
    }

    // ---- In flight: 40 Hz for 6s ----
    if (launched && millis() - launchTime <= 6000) {
      static unsigned long lastSample = 0;
      if (millis() - lastSample >= 25) {
        bmp.measure();
        float alt = bmp.getRelativeAltitudeM();
        if (alt > maxAltitude) maxAltitude = alt;
        lastSample = millis();
      }
      continue;
    }

    // ---- Landed idle loop ----
    delay(200); // light idle delay so we don't miss presses
  }
}
