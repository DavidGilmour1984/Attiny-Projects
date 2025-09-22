// ===== ATtiny85 + BMP280 Rocket Logger =====
// Flashes out max altitude with digit+separator scheme
// Zero digits = long flash on LED_DIGIT
// Separator = LED_SEP, only between digits (not after last)
// ---------------------------------------------------
// Pinout:
//   PB1 (pin 6)   -> SWITCH (internal pull-up, active LOW)
//   PB3 (pin 2)   -> LED_DIGIT (digit flashes, incl. zero)
//   PB4 (pin 3)   -> LED_SEP   (separator blink)

#include "ATTINY85BMP280.h"
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>

ATTINY85BMP280 bmp;

// ==== Pin definitions ====
#define LED_DIGIT    3   // PB3
#define LED_SEP      4   // PB4
#define SWITCH_PIN   1   // PB1

// ==== Timing (ms) ====
#define FLASH_ON_TIME    20     // LED ON for normal digit flash
#define FLASH_OFF_TIME   800    // gap between flashes
#define DIGIT_GAP_TIME   2000    // pause after separator before next digit
#define ZERO_FLASH_TIME  (FLASH_ON_TIME * 4) // long flash for zero
#define SEPARATOR_TIME   200    // separator ON time

// ==== State tracking ====
bool launched = false;
unsigned long launchTime = 0;
float maxAltitude = 0;
bool heartbeatToggle = false;

// ==== Watchdog ISR ====
ISR(WDT_vect) {
  // wakes from sleep
}

// ---- Sleep helper ----
void sleepSeconds(byte seconds) {
  for (byte i = 0; i < seconds; i++) {
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();

    WDTCR = (1<<WDCE) | (1<<WDE);
    WDTCR = (1<<WDP2) | (1<<WDP1); // 1s
    WDTCR |= (1<<WDIE);            // enable interrupt

    sleep_cpu();
    sleep_disable();
    wdt_disable();
  }
}

// ---------------------------------------------------
// Heartbeat alternates LEDs
void heartbeat() {
  if (heartbeatToggle) {
    digitalWrite(LED_DIGIT, HIGH); delay(50); digitalWrite(LED_DIGIT, LOW);
  } else {
    digitalWrite(LED_SEP, HIGH); delay(50); digitalWrite(LED_SEP, LOW);
  }
  heartbeatToggle = !heartbeatToggle;
}

// Flash a single digit (1–9 = short flashes, 0 = long flash)
void flashDigit(int digit) {
  if (digit == 0) {
    digitalWrite(LED_DIGIT, HIGH);
    delay(ZERO_FLASH_TIME);
    digitalWrite(LED_DIGIT, LOW);
    delay(FLASH_OFF_TIME);
  } else {
    for (int i = 0; i < digit; i++) {
      digitalWrite(LED_DIGIT, HIGH);
      delay(FLASH_ON_TIME);
      digitalWrite(LED_DIGIT, LOW);
      delay(FLASH_OFF_TIME);
    }
  }
}

// Separator blink
void flashSeparator() {
  digitalWrite(LED_SEP, HIGH);
  delay(SEPARATOR_TIME);
  digitalWrite(LED_SEP, LOW);
  delay(DIGIT_GAP_TIME);
}

// Output altitude as digits (thousands→hundreds→tens→ones)
void flashAltitudeDecimal(long altitude) {
  int thousands = altitude / 1000;
  int hundreds  = (altitude / 100) % 10;
  int tens      = (altitude / 10) % 10;
  int ones      = altitude % 10;

  bool started = false;

  if (thousands > 0) {
    flashDigit(thousands);
    flashSeparator();
    started = true;
  }

  if (started || hundreds > 0) {
    flashDigit(hundreds);
    flashSeparator();
    started = true;
  }

  if (started || tens > 0) {
    flashDigit(tens);
    flashSeparator();
    started = true;
  }

  // Always show ones digit, no separator after
  flashDigit(ones);
}

// ---------------------------------------------------
void setup() {
  pinMode(LED_DIGIT, OUTPUT);
  pinMode(LED_SEP, OUTPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP);

  bmp.begin();

  long sum = 0;
  for (int i = 0; i < 20; i++) {
    bmp.measure();
    sum += bmp.getPressurePa();
    delay(50);
  }
  bmp.setBaselinePressure(sum / 20);

  heartbeat();
}

// ---------------------------------------------------
void loop() {
  // ---- Pre-launch ----
  if (!launched) {
    static unsigned long beatCounter = 0;

    bmp.measure();
    float alt = bmp.getRelativeAltitudeM();

    if (beatCounter % 2 == 0) {
      heartbeat();
    }

    if (alt > 5) {
      launched = true;
      launchTime = millis();
      maxAltitude = alt;
    }

    beatCounter++;
    sleepSeconds(1);
    return;
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
    return;
  }

  // ---- Landed ----
  while (true) {
    if (digitalRead(SWITCH_PIN) == LOW) {
      delay(2000);
      flashAltitudeDecimal((long)maxAltitude);
    }
    sleepSeconds(1);
  }
}
