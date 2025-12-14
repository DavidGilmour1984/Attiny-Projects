/*
  ==========================================================
   ATtiny13A 5-Step PWM Dimmer
   - Starts ASLEEP
   - Latched ON / OFF via long press
   - Auto sleep after 2 hours
  ==========================================================

   Physical pin 5 (PB0) → ON / OFF (active LOW, pull-up)
   Physical pin 3 (PB4) → Brightness button (active LOW)
   Physical pin 6 (PB1) → PWM output

   - 0.5 s hold to toggle ON / OFF
   - 0.5 s hold to change brightness
   - Starts at 20% brightness after wake
   - Auto sleeps after 2 hours
*/

#define F_CPU 9600000UL
#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <util/delay.h>

// ----------------------------------------------------------
#define PWM_PERIOD_US 2000
#define HOLD_TIME_MS  500
#define AUTO_SLEEP_MS (2UL * 60UL * 60UL * 1000UL)   // 2 hours

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
  DDRB &= ~((1 << PB0) | (1 << PB4));      // buttons

  PORTB |= (1 << PB0) | (1 << PB4);        // pull-ups

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
      if (t >= HOLD_TIME_MS) {
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

    // ------------------------------------------------------
    // ON state loop
    // ------------------------------------------------------
    uint16_t offTimer = 0;
    uint16_t brightTimer = 0;
    uint8_t offHandled = 0;
    uint8_t brightHandled = 0;

    unsigned long awakeTime = 0;   // ms since wake

    while (onState) {

      // ---------------- Auto sleep timer ------------------
      if (awakeTime >= AUTO_SLEEP_MS) {
        onState = 0;
        goToSleep();
        break;
      }

      // ---------------- ON/OFF button ---------------------
      if (!(PINB & (1 << PB0))) {
        if (!offHandled) {
          offTimer++;
          if (offTimer >= HOLD_TIME_MS) {
            offHandled = 1;
            onState = 0;
            goToSleep();
            break;
          }
        }
      } else {
        offTimer = 0;
        offHandled = 0;
      }

      // ---------------- Brightness button -----------------
      if (!(PINB & (1 << PB4))) {
        if (!brightHandled) {
          brightTimer++;
          if (brightTimer >= HOLD_TIME_MS) {
            brightHandled = 1;
            level++;
            if (level > 4) level = 0;
          }
        }
      } else {
        brightTimer = 0;
        brightHandled = 0;
      }

      // ---------------- PWM output ------------------------
      uint8_t duty = dutyTable[level];
      uint16_t onTime  = (PWM_PERIOD_US * duty) / 100;
      uint16_t offTime = PWM_PERIOD_US - onTime;

      PORTB |= (1 << PB1);
      delay_us_var(onTime);

      PORTB &= ~(1 << PB1);
      delay_us_var(offTime);

      // ---------------- Time accounting -------------------
      awakeTime += (PWM_PERIOD_US / 1000);   // ≈ ms per cycle
    }
  }
}
