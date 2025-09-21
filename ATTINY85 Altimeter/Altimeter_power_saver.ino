// set clock speed to 1mhz.
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

// ===== Flash timing (ms) =====
#define FLASH_ON_TIME   30    // LED on-time per flash
#define FLASH_GAP_TIME  400   // gap between flashes
#define DIGIT_GAP_TIME  600   // pause between digits

// ===== Heartbeat =====
void heartbeat() {
  digitalWrite(LED_ONES, HIGH);  delay(FLASH_ON_TIME); digitalWrite(LED_ONES, LOW);
  digitalWrite(LED_TENS, HIGH);  delay(FLASH_ON_TIME); digitalWrite(LED_TENS, LOW);
  digitalWrite(LED_HUNDREDS, HIGH); delay(FLASH_ON_TIME); digitalWrite(LED_HUNDREDS, LOW);
}

// ===== Non-blocking altitude flash state =====
struct FlashState {
  int digits[4];         // thousands, hundreds, tens, ones
  int currentDigit = 0;  // which digit we are flashing
  int flashesDone = 0;   // flashes done for this digit
  bool ledOn = false;
  unsigned long lastChange = 0;
};

FlashState flashState;
bool flashing = false;

// ===== Start altitude flashing =====
void startFlashAltitude(int altitude) {
  flashState.digits[0] = (altitude / 1000) % 10;
  flashState.digits[1] = (altitude / 100) % 10;
  flashState.digits[2] = (altitude / 10) % 10;
  flashState.digits[3] = altitude % 10;
  flashState.currentDigit = 0;
  flashState.flashesDone = 0;
  flashState.ledOn = false;
  flashState.lastChange = millis();
  flashing = true;
}

// ===== Call this from loop() continuously =====
void updateFlashAltitude() {
  if (!flashing) return;

  unsigned long now = millis();

  // Digit → pin mapping
  int pins[4] = {LED_HUNDREDS, LED_HUNDREDS, LED_TENS, LED_ONES};
  int counts[4] = {
    flashState.digits[0], // thousands (special case = all LEDs)
    flashState.digits[1], // hundreds
    flashState.digits[2], // tens
    flashState.digits[3]  // ones
  };

  // Special case: thousands digit → flash BOTH HUNDREDS+ONES LEDs
  if (flashState.currentDigit == 0 && counts[0] > 0) {
    if (flashState.ledOn) {
      if (now - flashState.lastChange >= FLASH_ON_TIME) {
        digitalWrite(LED_HUNDREDS, LOW);
        digitalWrite(LED_ONES, LOW);
        flashState.ledOn = false;
        flashState.lastChange = now;
        flashState.flashesDone++;
      }
    } else {
      if (flashState.flashesDone < counts[0]) {
        if (now - flashState.lastChange >= FLASH_GAP_TIME) {
          digitalWrite(LED_HUNDREDS, HIGH);
          digitalWrite(LED_ONES, HIGH);
          flashState.ledOn = true;
          flashState.lastChange = now;
        }
      } else {
        flashState.currentDigit++;
        flashState.flashesDone = 0;
        flashState.lastChange = now;
      }
    }
    return;
  }

  // For hundreds/tens/ones
  if (flashState.currentDigit < 4) {
    int pin = pins[flashState.currentDigit];
    int count = counts[flashState.currentDigit];

    if (count == 0) {
      // skip if digit is zero
      flashState.currentDigit++;
      flashState.flashesDone = 0;
      flashState.lastChange = now;
      return;
    }

    if (flashState.ledOn) {
      if (now - flashState.lastChange >= FLASH_ON_TIME) {
        digitalWrite(pin, LOW);
        flashState.ledOn = false;
        flashState.lastChange = now;
        flashState.flashesDone++;
      }
    } else {
      if (flashState.flashesDone < count) {
        if (now - flashState.lastChange >= FLASH_GAP_TIME) {
          digitalWrite(pin, HIGH);
          flashState.ledOn = true;
          flashState.lastChange = now;
        }
      } else {
        // finished this digit → spacing before next digit
        if (now - flashState.lastChange >= DIGIT_GAP_TIME) {
          flashState.currentDigit++;
          flashState.flashesDone = 0;
          flashState.lastChange = now;
        }
      }
    }
  } else {
    flashing = false; // finished all digits
  }
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
    if (millis() - lastBeat > 10000) {
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
      startFlashAltitude((int)maxAltitude);  // start non-blocking flashes
    }
  }

  if (landed) {
    updateFlashAltitude();  // keep flashing altitude

    if (!flashing) {
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
}
