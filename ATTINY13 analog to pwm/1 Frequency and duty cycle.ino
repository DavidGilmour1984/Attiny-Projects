/*
  ==========================================================
   ATtiny13A ADC → Frequency + Duty Cycle Generator
  ==========================================================

   - Pin 3 → PB4 (ADC2): Frequency control (10–500 Hz)
   - Pin 2 → PB3 (ADC3): Duty cycle control (0–50%)
   - Pin 7 → PB2: Output signal
   - Pin 5 → PB0: Button (LOW = wake/sleep toggle)

   Behaviour:
     • Starts in deep sleep (power-down)
     • PB0 LOW → wakes chip
     • PB0 LOW again → goes back to sleep
     • Output: variable frequency + duty cycle
*/

#define F_CPU 9600000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>

// === Map function ===
long mapValue(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// === Variable delay in µs ===
void delay_us_var(uint16_t us) {
  while (us >= 10) {
    _delay_us(10);
    us -= 10;
  }
  while (us--) {
    _delay_us(1);
  }
}

// === ADC Read ===
uint16_t readADC(uint8_t channel) {
  ADMUX = (channel & 0x03);                // select channel
  ADCSRA |= (1 << ADSC);                   // start conversion
  while (ADCSRA & (1 << ADSC));            // wait
  return ADCW;                             // 0–1023
}

// === State flag ===
volatile uint8_t wakeFlag = 0;

// === Pin change interrupt (PB0) ===
ISR(PCINT0_vect) {
  if (!(PINB & (1 << PB0))) {
    wakeFlag = !wakeFlag;
  }
}

int main(void) {
  // === Pin setup ===
  DDRB |= (1 << PB2);    // PB2 = output
  DDRB &= ~(1 << PB0);   // PB0 = input
  PORTB |= (1 << PB0);   // enable pull-up on PB0

  // === ADC setup ===
  ADMUX  = 0;                                     // right adjust
  ADCSRA = (1 << ADEN) | (1 << ADPS1) | (1 << ADPS0); // enable, prescaler 8

  // === Enable pin change interrupt on PB0 ===
  GIMSK |= (1 << PCIE);
  PCMSK |= (1 << PCINT0);
  sei();

  // === Setup sleep mode ===
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);

  // === Start in sleep ===
  sleep_enable();
  sleep_cpu();

  while (1) {
    if (wakeFlag) {
      while (wakeFlag) {
        // read ADCs live
        uint16_t adc_freq  = readADC(2);
        uint16_t adc_duty  = readADC(3);

        uint16_t freq = mapValue(adc_freq, 0, 1023, 10, 500);
        uint16_t halfPeriod = (500000UL / freq);

        // duty cycle: 0–50%
        uint16_t duty = mapValue(adc_duty, 0, 1023, 0, 50);
        uint32_t period = 1000000UL / freq;
        uint32_t onTime  = (period * duty) / 100;
        uint32_t offTime = period - onTime;

        // --- Generate one cycle ---
        if (onTime > 0) {
          PORTB |= (1 << PB2);
          delay_us_var(onTime);
        }

        PORTB &= ~(1 << PB2);
        if (offTime > 0) {
          delay_us_var(offTime);
        }
      }
    }

    // === Sleep mode ===
    PORTB &= ~(1 << PB2);
    sleep_enable();
    sleep_cpu();
  }
}
