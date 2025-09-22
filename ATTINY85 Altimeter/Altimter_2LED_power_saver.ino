/*
  ==============================================================
   ATtiny85 + BMP280 Rocket Altitude Logger
  ==============================================================

   Overview:
   - Simple rocket flight computer using ATtiny85 and BMP280
   - Detects launch (>5 m rise), logs altitude at 40 Hz for 6 s
   - Stores maximum altitude
   - After flight, altitude is displayed with LEDs:
       • LED_DIGIT flashes out digits (1–9 = short flashes, 0 = one long flash)
       • LED_SEP blinks once as a separator between digits
       • No separator after the last digit
   - Output triggered by pressing a switch (PB1 LOW, 2 s debounce delay)

   Example readouts:
     Altitude 120 m → 1 flash, separator, 2 flashes, separator, long flash
     Altitude 304 m → 3 flashes, separator, long flash, separator, 4 flashes
     Altitude 7 m   → 7 flashes (no separator at end)

   Power behaviour:
   - Sleeps between pre-launch altitude samples (1 Hz sampling)
   - Active at 40 Hz during flight for 6 s only
   - After flight, stays in deep sleep until switch is pressed
   - Very low idle current (<10 µA), battery life limited by coin cell shelf life

   --------------------------------------------------------------
   Pinout (ATtiny85 physical pins):
   - PB1 (pin 6) : SWITCH (internal pull-up, active LOW)
   - PB3 (pin 2) : LED_DIGIT (digit flashes, incl. zero as long flash)
   - PB4 (pin 3) : LED_SEP   (separator blink)
   - PB0 / PB2   : I²C lines for BMP280 (handled by ATTINY85BMP280 library)

   Timing constants (default):
   - Normal flash: 20 ms ON, 800 ms OFF
   - Zero flash: 80 ms ON (4× longer than normal)
   - Separator: 200 ms ON, then 2000 ms gap before next digit

   --------------------------------------------------------------
*/


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
#define ZERO_FLASH_TIME  (FLASH_ON_TIME * 40) // long flash for zero
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
