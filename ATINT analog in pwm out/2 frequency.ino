/*
  ==========================================================
   ATtiny13A Dual ADC → Frequency Generator
  ==========================================================

   - Reads ADC values from physical pins:
       Pin 2 → PB3 (ADC3)
       Pin 3 → PB4 (ADC2)
   - Maps values to frequencies 100–550 Hz
   - Outputs square waves on:
       Pin 6 → PB1
       Pin 7 → PB2
   - Uses loop-based delay (safe for variable times)
   - Works best with ATtiny13A clock set to 9.6 MHz
*/

#include <avr/io.h>
#include <util/delay.h>

// === Arduino-like map ===
long mapValue(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// === Variable delay in µs using loops ===
void delay_us_var(uint16_t us) {
  while (us >= 10) {
    _delay_us(10);   // fixed, compiler-safe
    us -= 10;
  }
  while (us--) {
    _delay_us(1);    // mop up the remainder
  }
}

// === ADC Read ===
uint16_t readADC(uint8_t channel) {
  ADMUX = (1 << ADLAR) | (channel & 0x03); // left adjust, select channel
  ADCSRA |= (1 << ADSC);                   // start conversion
  while (ADCSRA & (1 << ADSC));            // wait for conversion
  return ADCW;                             // return result
}

int main(void) {
  // === Pin setup ===
  DDRB |= (1 << PB1) | (1 << PB2); // PB1 (pin 6) and PB2 (pin 7) as outputs

  // === ADC setup ===
  ADMUX  = (1 << ADLAR);                          // left adjust result
  ADCSRA = (1 << ADEN) | (1 << ADPS1) | (1 << ADPS0); // enable, prescaler 8

  while (1) {
    // Read analog inputs
    uint16_t adc2 = readADC(2); // Pin 3 → ADC2
    uint16_t adc3 = readADC(3); // Pin 2 → ADC3

    // Map ADC values to frequency (100–550 Hz)
    uint16_t freq1 = mapValue(adc2, 0, 1023, 100, 550);
    uint16_t freq2 = mapValue(adc3, 0, 1023, 100, 550);

    // Half-period in µs
    uint16_t halfPeriod1 = (500000UL / freq1);
    uint16_t halfPeriod2 = (500000UL / freq2);

    // Toggle PB1
    PORTB ^= (1 << PB1);
    delay_us_var(halfPeriod1);

    // Toggle PB2
    PORTB ^= (1 << PB2);
    delay_us_var(halfPeriod2);
  }
}
