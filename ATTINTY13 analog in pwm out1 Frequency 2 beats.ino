/*
  ==========================================================
   ATtiny13A ADC → Frequency Pulse Generator
  ==========================================================

   - Pin 3 → PB4 (ADC2): Frequency control (10–500 Hz)
   - Pin 2 → PB3 (ADC3): Beat length control (50–500 ms)
   - Pin 7 → PB2: Output tone
   - Behaviour:
       • Square wave (50% duty) at chosen frequency
       • ON for X ms, OFF for X ms
       • X = 50–500 ms, adjustable live
*/

#define F_CPU 9600000UL   // ensure this matches your fuse setting!
#include <avr/io.h>
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

int main(void) {
  // === Pin setup ===
  DDRB |= (1 << PB2); // PB2 (pin 7) output

  // === ADC setup ===
  ADMUX  = 0;                                     // right adjust
  ADCSRA = (1 << ADEN) | (1 << ADPS1) | (1 << ADPS0); // enable, prescaler 8

  while (1) {
    // --- ON phase ---
    unsigned long elapsed = 0;
    uint16_t pulse_ms = mapValue(readADC(3), 0, 1023, 50, 500);

    while (elapsed < pulse_ms) {
      // read ADCs live
      uint16_t adc_freq  = readADC(2);
      uint16_t adc_pulse = readADC(3);

      uint16_t freq = mapValue(adc_freq, 0, 1023, 10, 500);
      pulse_ms = mapValue(adc_pulse, 0, 1023, 50, 500);

      uint16_t halfPeriod = (500000UL / freq);

      // toggle once
      PORTB |= (1 << PB2);
      delay_us_var(halfPeriod);
      PORTB &= ~(1 << PB2);
      delay_us_var(halfPeriod);

      elapsed += (2UL * halfPeriod) / 1000; // ms counter
    }

    // --- OFF phase ---
    elapsed = 0;
    pulse_ms = mapValue(readADC(3), 0, 1023, 50, 500);

    while (elapsed < pulse_ms) {
      PORTB &= ~(1 << PB2);

      pulse_ms = mapValue(readADC(3), 0, 1023, 50, 500); // live adjust

      _delay_ms(1);
      elapsed++;
    }
  }
}
