// set clock speed to 1mhz
#include "ATTINY85BMP280.h"
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>

ATTINY85BMP280 bmp;

// ===== LED pins (Arduino numbering, not physical pins) =====
#define LED_ONES     1   // physical pin 6 (PB1)
#define LED_TENS     3   // physical pin 2 (PB3)
#define LED_HUNDREDS 4   // physical pin 3 (PB4)

// ===== State tracking =====
bool launched = false;
unsigned long launchTime = 0;
float maxAltitude = 0;

// ===== Functions =====
void heartbeat() {
  digitalWrite(LED_ONES, HIGH); delay(50); digitalWrite(LED_ONES, LOW);
  digitalWrite(LED_TENS, HIGH); delay(50); digitalWrite(LED_TENS, LOW);
  digitalWrite(LED_HUNDREDS, HIGH); delay(50); digitalWrite(LED_HUNDREDS, LOW);
}

void flashDigit(int pin, int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(pin, HIGH);
    delay(15);
    digitalWrite(pin, LOW);
    delay(500);
  }
  delay(800); // spacing between digits
}

void flashAltitude(int altitude) {
  int thousands = (altitude / 1000) % 10;
  int hundreds  = (altitude / 100) % 10;
  int tens      = (altitude / 10) % 10;
  int ones      = altitude % 10;

  // Thousands → flash hundreds + ones at the same time
  if (thousands > 0) {
    for (int i = 0; i < thousands; i++) {
      digitalWrite(LED_HUNDREDS, HIGH);
      digitalWrite(LED_ONES, HIGH);
      delay(15);
      digitalWrite(LED_HUNDREDS, LOW);
      digitalWrite(LED_ONES, LOW);
      delay(500);
    }
    delay(800); // spacing between thousands and rest
  }

  if (hundreds > 0) flashDigit(LED_HUNDREDS, hundreds);
  if (tens > 0)     flashDigit(LED_TENS, tens);
  flashDigit(LED_ONES, ones);
}

// ==== Watchdog ISR (wake from sleep) ====
ISR(WDT_vect) {
  // Just wakes up, nothing else
}

// ---- Sleep helper (1s chunks) ----
void sleepSeconds(byte seconds) {
  for (byte i = 0; i < seconds; i++) {
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();

    // configure watchdog for 1s timeout
    WDTCR = (1<<WDCE) | (1<<WDE);
    WDTCR = (1<<WDP2) | (1<<WDP1); // 1s prescaler
    WDTCR |= (1<<WDIE);            // enable interrupt

    sleep_cpu();   // go to sleep
    sleep_disable();
    wdt_disable();
  }
}

void setup() {
  pinMode(LED_ONES, OUTPUT);
  pinMode(LED_TENS, OUTPUT);
  pinMode(LED_HUNDREDS, OUTPUT);

  bmp.begin();

  // Discard first few samples and average baseline
  long sum = 0;
  for (int i = 0; i < 20; i++) {
    bmp.measure();
    sum += bmp.getPressurePa();
    delay(50);
  }
  bmp.setBaselinePressure(sum / 20);

  heartbeat(); // quick heartbeat at boot
}

void loop() {
  // ---- Pre-launch ----
  if (!launched) {
    static unsigned long beatCounter = 0;

    bmp.measure();
    float alt = bmp.getRelativeAltitudeM();

    // heartbeat every ~2s
    if (beatCounter % 5 == 0) {
      heartbeat();
    }

    if (alt > 5) {
      launched = true;
      launchTime = millis();
      maxAltitude = alt;
    }

    beatCounter++;
    sleepSeconds(1);  // sleep between pre-launch samples
    return;
  }

  // ---- In flight: sample at 20Hz for 6s ----
  if (launched && millis() - launchTime <= 6000) {
    static unsigned long lastSample = 0;
    if (millis() - lastSample >= 50) {
      bmp.measure();
      float alt = bmp.getRelativeAltitudeM();
      if (alt > maxAltitude) maxAltitude = alt;
      lastSample = millis();
    }
    return;
  }

  // ---- Landed: flash altitude then sleep for ~10s ----
  flashAltitude((int)maxAltitude);
  sleepSeconds(20);
}
