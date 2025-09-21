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
bool landed   = false;
unsigned long launchTime = 0;
float maxAltitude = 0;

// ===== Functions =====
void heartbeat() {
  digitalWrite(LED_ONES, HIGH); delay(100); digitalWrite(LED_ONES, LOW);
  digitalWrite(LED_TENS, HIGH); delay(100); digitalWrite(LED_TENS, LOW);
  digitalWrite(LED_HUNDREDS, HIGH); delay(100); digitalWrite(LED_HUNDREDS, LOW);
}

void flashDigit(int pin, int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(pin, HIGH);
    delay(200);
    digitalWrite(pin, LOW);
    delay(200);
  }
  delay(600); // spacing between digits
}

void flashAltitude(int altitude) {
  int thousands = (altitude / 1000) % 10;
  int hundreds  = (altitude / 100) % 10;
  int tens      = (altitude / 10) % 10;
  int ones      = altitude % 10;

  if (thousands > 0) {
    for (int i = 0; i < thousands; i++) {
      digitalWrite(LED_HUNDREDS, HIGH);
      digitalWrite(LED_ONES, HIGH);
      delay(200);
      digitalWrite(LED_HUNDREDS, LOW);
      digitalWrite(LED_ONES, LOW);
      delay(200);
    }
    delay(600);
  }

  if (hundreds > 0) flashDigit(LED_HUNDREDS, hundreds);
  if (tens > 0)     flashDigit(LED_TENS, tens);
  flashDigit(LED_ONES, ones);
}

// ===== Watchdog ISR =====
ISR(WDT_vect) {
  // empty ISR — just wakes the MCU
}

void setup() {
  pinMode(LED_ONES, OUTPUT);
  pinMode(LED_TENS, OUTPUT);
  pinMode(LED_HUNDREDS, OUTPUT);

  bmp.begin();

  // Average baseline
  long sum = 0;
  for (int i = 0; i < 20; i++) {
    bmp.measure();
    sum += bmp.getPressurePa();
    delay(50);
  }
  bmp.setBaselinePressure(sum / 20);

  heartbeat();
}

void loop() {
  if (!launched) {
    static unsigned long lastBeat = 0;
    if (millis() - lastBeat > 2000) {
      heartbeat();
      lastBeat = millis();
    }

    static unsigned long lastCheck = 0;
    if (millis() - lastCheck >= 1000) {
      bmp.measure();
      float alt = bmp.getRelativeAltitudeM();
      if (alt > 5) {
        launched = true;
        launchTime = millis();
        maxAltitude = alt;
      }
      lastCheck = millis();
    }
    return;
  }

  if (launched && !landed) {
    if (millis() - launchTime <= 6000) {
      static unsigned long lastSample = 0;
      if (millis() - lastSample >= 50) {
        bmp.measure();
        float alt = bmp.getRelativeAltitudeM();
        if (alt > maxAltitude) maxAltitude = alt;
        lastSample = millis();
      }
      return;
    } else {
      landed = true;
    }
  }

  if (landed) {
    flashAltitude((int)maxAltitude);

    // --- Watchdog interrupt sleep (no reset) ---
    cli(); // disable interrupts
    MCUSR &= ~(1<<WDRF); // clear reset flag
    WDTCR |= (1<<WDCE) | (1<<WDE); // enable config
    WDTCR = (1<<WDIE) | (1<<WDP3) | (1<<WDP0); // interrupt, ~8s
    sei(); // enable interrupts

set_sleep_mode(SLEEP_MODE_PWR_DOWN);
sleep_enable();

// clear watchdog interrupt flag
WDTCR |= (1 << WDIF);

sleep_cpu();   // go to sleep until watchdog interrupt
sleep_disable();


  }
}
