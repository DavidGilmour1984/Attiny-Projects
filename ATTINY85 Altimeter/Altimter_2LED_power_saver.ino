/*
  ==============================================================
   ATtiny85 + BMP280 Rocket Altitude Logger
  ==============================================================

   Overview:
   - Simple rocket flight computer using ATtiny85 and BMP280
   - Detects launch (>5 m rise), logs altitude at 40 Hz for 6 s
   - Stores maximum altitude
   - After flight, altitude is displayed with LEDs:
       • PLACE LED flashes first:
            - 4 flashes = thousands place
            - 3 flashes = hundreds place
            - 2 flashes = tens place
            - 1 flash   = ones place
       • VALUE LED flashes next = digit in that place
   - Output triggered by pressing a switch (PB1 LOW, 2 s debounce delay)

   Example readouts:
     Altitude 381 m →
       3 place flashes, 3 value flashes (hundreds)
       2 place flashes, 8 value flashes (tens)
       1 place flash,  1 value flash  (ones)
*/

#include "ATTINY85BMP280.h"
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>

ATTINY85BMP280 bmp;

// ==== Pin definitions ====
#define LED_VALUE    3   // PB3 = flashes digit value
#define LED_PLACE    4   // PB4 = flashes place value
#define SWITCH_PIN   1   // PB1 = trigger switch

// ==== Timing (ms) ====
#define FLASH_ON_TIME    20     // LED ON for normal flash
#define FLASH_OFF_TIME   800    // gap between flashes
#define ZERO_FLASH_TIME  (FLASH_ON_TIME * 40) // long flash for zero

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
    digitalWrite(LED_VALUE, HIGH); delay(50); digitalWrite(LED_VALUE, LOW);
  } else {
    digitalWrite(LED_PLACE, HIGH); delay(50); digitalWrite(LED_PLACE, LOW);
  }
  heartbeatToggle = !heartbeatToggle;
}

// Flash helper for either LED
void flashLED(int pin, int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(pin, HIGH);
    delay(FLASH_ON_TIME);
    digitalWrite(pin, LOW);
    delay(FLASH_OFF_TIME);
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
    flashLED(LED_PLACE, 4);          // thousands place
    flashLED(LED_VALUE, thousands);  // value
  }

  if (hundreds > 0 || thousands > 0) {
    flashLED(LED_PLACE, 3);          // hundreds place
    flashLED(LED_VALUE, hundreds);   // value
  }

  if (tens > 0 || hundreds > 0 || thousands > 0) {
    flashLED(LED_PLACE, 2);          // tens place
    flashLED(LED_VALUE, tens);       // value
  }

  // always show ones
  flashLED(LED_PLACE, 1);            // ones place
  flashLED(LED_VALUE, ones);         // value
}

// ---------------------------------------------------
void setup() {
  pinMode(LED_VALUE, OUTPUT);
  pinMode(LED_PLACE, OUTPUT);
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
      flashAltitudePlaceValue((long)maxAltitude);
    }
    sleepSeconds(1);
  }
}
