/*
  ==========================================================
   ATtiny13A Analog PWM Dimmer
   - Starts ASLEEP
   - Latched ON / OFF via long press
   - Auto sleep after 2 hours
   - Linear brightness set by analog input on PB3 (ADC3)
  ==========================================================

   Physical pin 5 (PB0) → ON / OFF (active LOW, pull-up)
   Physical pin 2 (PB3) → Brightness analog input (ADC3)
   Physical pin 6 (PB1) → PWM output

   - 0.5 s hold to toggle ON / OFF
   - Brightness follows voltage on PB3 (0–Vcc → 20–100%)
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

#define DUTY_MIN 20   // % at 0 V
#define DUTY_MAX 100  // % at Vcc

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
void adc_init(void) {
  ADMUX  = (1 << MUX1) | (1 << MUX0);   // ADC3 (PB3), Vcc reference
  ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1); // enable, /64
}

uint16_t adc_read(void) {
  ADCSRA |= (1 << ADSC);
  while (ADCSRA & (1 << ADSC));
  return ADC;
}

// Map 10-bit ADC (0–1023) linearly to DUTY_MIN..DUTY_MAX
uint8_t adc_to_duty(uint16_t val) {
  return DUTY_MIN + (uint16_t)((uint32_t)val * (DUTY_MAX - DUTY_MIN) / 1023);
}

// ----------------------------------------------------------
void goToSleep(void) {
  PORTB &= ~(1 << PB1);            // force output OFF
  ADCSRA &= ~(1 << ADEN);         // disable ADC to save power
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sei();
  sleep_cpu();
  sleep_disable();
  ADCSRA |= (1 << ADEN);          // re-enable ADC on wake
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
  DDRB &= ~((1 << PB0) | (1 << PB3));      // ON/OFF button + ADC input

  PORTB |= (1 << PB0);                     // pull-up on button only
  PORTB &= ~(1 << PB3);                    // no pull-up on analog input

  adc_init();

  // --------------------------------------------------------
  // Pin-change interrupt on PB0
  // --------------------------------------------------------
  GIMSK |= (1 << PCIE);
  PCMSK |= (1 << PCINT0);
  sei();

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
    uint8_t offHandled = 0;

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

      // ---------------- Brightness from ADC ---------------
      uint8_t duty = adc_to_duty(adc_read());

      // ---------------- PWM output ------------------------
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
