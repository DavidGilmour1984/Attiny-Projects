/*
  ==========================================================
   ATtiny13A 5-Step PWM Dimmer — Single Button
   - Starts ASLEEP
   - Hold 0.5 s while asleep : wake ON
   - Short press while ON    : cycle brightness
   - Hold 2 s while ON       : turn OFF
   - Auto sleep after 2 hours
  ==========================================================

   Physical pin 5 (PB0) → Power / Brightness button (active LOW, pull-up)
   Physical pin 3 (PB4) → Unused (pull-up enabled)
   Physical pin 6 (PB1) → PWM output
*/

#define F_CPU 9600000UL
#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <util/delay.h>

// ----------------------------------------------------------
#define PWM_PERIOD_US  2000
#define PWM_PERIOD_MS  (PWM_PERIOD_US / 1000)
#define WAKE_HOLD_MS   500
#define OFF_HOLD_MS    2000
#define DEBOUNCE_MS    30
#define AUTO_SLEEP_MS  (2UL * 60UL * 60UL * 1000UL)   // 2 hours

const uint8_t dutyTable[5] = {20, 40, 60, 80, 100};

// ----------------------------------------------------------
void delay_us_var(uint16_t us) {
  while (us >= 10) {
    _delay_us(10);
    us -= 10;
  }
  while (us--) {
    _delay_us(1);
  }
}

// ----------------------------------------------------------
void pwmCycle(uint8_t level) {
  uint16_t onTime  = (PWM_PERIOD_US * dutyTable[level]) / 100;
  uint16_t offTime = PWM_PERIOD_US - onTime;

  PORTB |= (1 << PB1);
  delay_us_var(onTime);

  PORTB &= ~(1 << PB1);
  delay_us_var(offTime);
}

// ----------------------------------------------------------
void goToSleep(void) {
  PORTB &= ~(1 << PB1);            // force output OFF
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sei();
  sleep_cpu();
  sleep_disable();
}

// ----------------------------------------------------------
ISR(PCINT0_vect) {
  // wake only
}

int main(void) {

  // --------------------------------------------------------
  // GPIO
  // --------------------------------------------------------
  DDRB |= (1 << PB1);                      // PWM output
  DDRB &= ~((1 << PB0) | (1 << PB4));      // inputs

  PORTB |= (1 << PB0) | (1 << PB4);        // pull-ups (PB4 unused, held high)

  // --------------------------------------------------------
  // Pin-change interrupt on PB0
  // --------------------------------------------------------
  GIMSK |= (1 << PCIE);
  PCMSK |= (1 << PCINT0);
  sei();

  uint8_t level = 0;   // brightness index (20%)
  uint8_t onState = 0; // START ASLEEP

  // --------------------------------------------------------
  // Immediately go to sleep on power-up
  // --------------------------------------------------------
  goToSleep();

  while (1) {

    // ------------------------------------------------------
    // We just woke — require 0.5 s LOW to latch ON
    // ------------------------------------------------------
    uint16_t t = 0;
    while (!(PINB & (1 << PB0))) {
      _delay_ms(1);
      t++;
      if (t >= WAKE_HOLD_MS) {
        onState = 1;
        level = 0;                 // reset brightness on wake
        break;
      }
    }

    // If button was not held long enough, sleep again
    if (!onState) {
      goToSleep();
      continue;
    }

    // Light output while waiting for the wake press to be released
    while (!(PINB & (1 << PB0))) {
      pwmCycle(level);
    }
    _delay_ms(DEBOUNCE_MS);

    // ------------------------------------------------------
    // ON state loop
    // ------------------------------------------------------
    uint16_t pressTime = 0;        // ms button has been held
    unsigned long awakeTime = 0;   // ms since wake

    while (onState) {

      // ---------------- Auto sleep timer ------------------
      if (awakeTime >= AUTO_SLEEP_MS) {
        onState = 0;
        goToSleep();
        break;
      }

      // ---------------- Single button ---------------------
      if (!(PINB & (1 << PB0))) {
        // Button held
        pressTime += PWM_PERIOD_MS;
        if (pressTime >= OFF_HOLD_MS) {
          onState = 0;             // 2 s hold → OFF
          goToSleep();
          break;
        }
      } else {
        // Button released
        if (pressTime >= DEBOUNCE_MS) {
          level++;                 // short press → next brightness
          if (level > 4) level = 0;
        }
        pressTime = 0;
      }

      // ---------------- PWM output ------------------------
      pwmCycle(level);

      // ---------------- Time accounting -------------------
      awakeTime += PWM_PERIOD_MS;
    }
  }
}
