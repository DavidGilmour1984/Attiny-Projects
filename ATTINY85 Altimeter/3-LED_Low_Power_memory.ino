// set clock speed to 1mhz
// Software I2C on swapped pins: SDA = PB2 (physical pin 7), SCL = PB0 (physical pin 5)
// Readout trigger: touch physical pin 2 (PB3, tens LED) to VCC, then release
// Last flight altitude is stored in EEPROM and survives power-off
// Power cycle (switch off/on) re-arms for a new flight
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <avr/power.h>
#include <avr/eeprom.h>

// ===== I2C pins (PORTB bit numbers) =====
#define I2C_SDA_BIT 2   // physical pin 7 (PB2)
#define I2C_SCL_BIT 0   // physical pin 5 (PB0)

// ===== LED pins (PORTB bit numbers) =====
#define LED_ONES     1   // physical pin 6 (PB1)
#define LED_TENS     3   // physical pin 2 (PB3)
#define LED_HUNDREDS 4   // physical pin 3 (PB4)
#define TRIGGER_BIT  LED_TENS

// ===== EEPROM layout =====
#define EE_MAGIC_ADDR ((uint8_t*)0)
#define EE_ALT_ADDR   ((uint16_t*)1)
#define EE_MAGIC      0xA5

// ===== Flight settings =====
#define LAUNCH_DROP_PA   60     // ~5 m (about 12 Pa per metre near sea level)
#define FLIGHT_WINDOW_MS 6000   // sample for 6 s after launch

// ===== Software I2C (open-drain emulation) =====
static inline void i2cDelay() { delayMicroseconds(2); }
static inline void sdaLow()   { DDRB |=  (1 << I2C_SDA_BIT); }
static inline void sdaHigh()  { DDRB &= ~(1 << I2C_SDA_BIT); }
static inline void sclLow()   { DDRB |=  (1 << I2C_SCL_BIT); }
static inline void sclHigh()  { DDRB &= ~(1 << I2C_SCL_BIT); }
static inline bool sdaRead()  { return PINB & (1 << I2C_SDA_BIT); }

void i2cInit() {
  PORTB &= ~((1 << I2C_SDA_BIT) | (1 << I2C_SCL_BIT)); // outputs drive low only
  sdaHigh();
  sclHigh();
}

void i2cStart() {
  sdaHigh(); sclHigh(); i2cDelay();
  sdaLow();  i2cDelay();
  sclLow();  i2cDelay();
}

void i2cStop() {
  sdaLow();  i2cDelay();
  sclHigh(); i2cDelay();
  sdaHigh(); i2cDelay();
}

bool i2cWrite(uint8_t data) {
  for (uint8_t i = 0; i < 8; i++) {
    if (data & 0x80) sdaHigh(); else sdaLow();
    i2cDelay();
    sclHigh(); i2cDelay();
    sclLow();
    data <<= 1;
  }
  sdaHigh(); i2cDelay();
  sclHigh(); i2cDelay();
  bool ack = !sdaRead();
  sclLow();  i2cDelay();
  return ack;
}

uint8_t i2cRead(bool ack) {
  uint8_t data = 0;
  sdaHigh();
  for (uint8_t i = 0; i < 8; i++) {
    data <<= 1;
    sclHigh(); i2cDelay();
    if (sdaRead()) data |= 1;
    sclLow();  i2cDelay();
  }
  if (ack) sdaLow(); else sdaHigh();
  i2cDelay();
  sclHigh(); i2cDelay();
  sclLow();
  sdaHigh(); i2cDelay();
  return data;
}

// ===== Minimal BMP280 driver (same API as before) =====
class ATTINY85BMP280 {
public:
  bool begin() {
    i2cInit();
    delay(10);
    addr = 0x76;
    if (readReg(0xD0) != 0x58) {
      addr = 0x77;
      if (readReg(0xD0) != 0x58) return false;
    }
    uint8_t c[24];
    readRegs(0x88, c, 24);
    dig_T1 = (uint16_t)(c[1]  << 8 | c[0]);
    dig_T2 = (int16_t)(c[3]   << 8 | c[2]);
    dig_T3 = (int16_t)(c[5]   << 8 | c[4]);
    dig_P1 = (uint16_t)(c[7]  << 8 | c[6]);
    dig_P2 = (int16_t)(c[9]   << 8 | c[8]);
    dig_P3 = (int16_t)(c[11]  << 8 | c[10]);
    dig_P4 = (int16_t)(c[13]  << 8 | c[12]);
    dig_P5 = (int16_t)(c[15]  << 8 | c[14]);
    dig_P6 = (int16_t)(c[17]  << 8 | c[16]);
    dig_P7 = (int16_t)(c[19]  << 8 | c[18]);
    dig_P8 = (int16_t)(c[21]  << 8 | c[20]);
    dig_P9 = (int16_t)(c[23]  << 8 | c[22]);
    writeReg(0xF5, 0x00); // no IIR filter, standby unused in forced mode
    return true;
  }

  // 0x31 = temp x1, pressure x8, forced (flight/baseline)
  // 0x25 = temp x1, pressure x1, forced (low-power pre-launch)
  void measure(uint8_t ctrl = 0x31) {
    writeReg(0xF4, ctrl);
    delay(2);
    for (uint8_t i = 0; i < 50 && (readReg(0xF3) & 0x08); i++) delay(1);
    uint8_t d[6];
    readRegs(0xF7, d, 6);
    int32_t adcP = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
    int32_t adcT = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
    compensateT(adcT);
    pressurePa = compensateP(adcP);
  }

  int32_t getPressurePa() { return pressurePa; }

  void setBaselinePressure(int32_t p) { baselinePa = p; }

  float getRelativeAltitudeM() {
    if (baselinePa <= 0 || pressurePa <= 0) return 0;
    return 44330.0f * (1.0f - pow((float)pressurePa / (float)baselinePa, 0.190295f));
  }

private:
  uint8_t addr = 0x76;
  int32_t pressurePa = 0;
  int32_t baselinePa = 0;
  int32_t t_fine = 0;
  uint16_t dig_T1, dig_P1;
  int16_t dig_T2, dig_T3, dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;

  void writeReg(uint8_t reg, uint8_t val) {
    i2cStart();
    i2cWrite(addr << 1);
    i2cWrite(reg);
    i2cWrite(val);
    i2cStop();
  }

  void readRegs(uint8_t reg, uint8_t *buf, uint8_t len) {
    i2cStart();
    i2cWrite(addr << 1);
    i2cWrite(reg);
    i2cStart();
    i2cWrite((addr << 1) | 1);
    for (uint8_t i = 0; i < len; i++) buf[i] = i2cRead(i < len - 1);
    i2cStop();
  }

  uint8_t readReg(uint8_t reg) {
    uint8_t v;
    readRegs(reg, &v, 1);
    return v;
  }

  void compensateT(int32_t adc_T) {
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
    t_fine = var1 + var2;
  }

  int32_t compensateP(int32_t adc_P) {
    int32_t var1, var2;
    uint32_t p;
    var1 = (t_fine >> 1) - (int32_t)64000;
    var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) * ((int32_t)dig_P6);
    var2 = var2 + ((var1 * ((int32_t)dig_P5)) << 1);
    var2 = (var2 >> 2) + (((int32_t)dig_P4) << 16);
    var1 = ((((int32_t)dig_P3 * (((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) + ((((int32_t)dig_P2) * var1) >> 1)) >> 18;
    var1 = ((((32768 + var1)) * ((int32_t)dig_P1)) >> 15);
    if (var1 == 0) return 0;
    p = (((uint32_t)(((int32_t)1048576) - adc_P) - (var2 >> 12))) * 3125;
    if (p < 0x80000000) p = (p << 1) / ((uint32_t)var1);
    else p = (p / (uint32_t)var1) * 2;
    var1 = (((int32_t)dig_P9) * ((int32_t)(((p >> 3) * (p >> 3)) >> 13))) >> 12;
    var2 = (((int32_t)(p >> 2)) * ((int32_t)dig_P8)) >> 13;
    p = (uint32_t)((int32_t)p + ((var1 + var2 + dig_P7) >> 4));
    return (int32_t)p;
  }
};

ATTINY85BMP280 bmp;

// ===== State tracking =====
volatile bool triggerFlag = false;
bool launched = false;
bool landed = false;
unsigned long launchTime = 0;
float maxAltitude = 0;
long baseline16 = 0;   // baseline pressure x16 (slowly follows weather)

// ===== LED control =====
// LEDs are only driven HIGH or left high-impedance, never driven LOW,
// so connecting VCC to an LED pin can never short an output.
void ledOn(uint8_t b)  { PORTB |= (1 << b); DDRB |= (1 << b); }
void ledOff(uint8_t b) { DDRB &= ~(1 << b); PORTB &= ~(1 << b); }

void bootFlash() {
  ledOn(LED_ONES);     delay(50); ledOff(LED_ONES);
  ledOn(LED_TENS);     delay(50); ledOff(LED_TENS);
  ledOn(LED_HUNDREDS); delay(50); ledOff(LED_HUNDREDS);
}

void heartbeat() {
  ledOn(LED_ONES); delay(15); ledOff(LED_ONES);
}

void noDataFlash() {
  for (uint8_t i = 0; i < 3; i++) {
    ledOn(LED_ONES); ledOn(LED_TENS); ledOn(LED_HUNDREDS);
    delay(15);
    ledOff(LED_ONES); ledOff(LED_TENS); ledOff(LED_HUNDREDS);
    delay(300);
  }
}

void flashDigit(uint8_t b, int count) {
  for (int i = 0; i < count; i++) {
    ledOn(b);
    delay(15);
    ledOff(b);
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
      ledOn(LED_HUNDREDS);
      ledOn(LED_ONES);
      delay(15);
      ledOff(LED_HUNDREDS);
      ledOff(LED_ONES);
      delay(500);
    }
    delay(800); // spacing between thousands and rest
  }

  if (hundreds > 0) flashDigit(LED_HUNDREDS, hundreds);
  if (tens > 0)     flashDigit(LED_TENS, tens);
  flashDigit(LED_ONES, ones);
}

// ===== EEPROM =====
void saveAltitude(int alt) {
  if (alt < 0) alt = 0;
  eeprom_update_word(EE_ALT_ADDR, (uint16_t)alt);
  eeprom_update_byte(EE_MAGIC_ADDR, EE_MAGIC);
}

void flashStored() {
  if (eeprom_read_byte(EE_MAGIC_ADDR) != EE_MAGIC) {
    noDataFlash();   // no flight recorded yet
    return;
  }
  flashAltitude((int)eeprom_read_word(EE_ALT_ADDR));
}

// ===== Readout trigger (pin change on PB3) =====
ISR(PCINT0_vect) {
  triggerFlag = true;
}

bool triggerHigh() { return PINB & (1 << TRIGGER_BIT); }

void triggerDisable() {
  PCMSK &= ~(1 << PCINT3);
}

void triggerEnable() {
  ledOff(TRIGGER_BIT);
  delay(5);
  GIFR = (1 << PCIF);      // clear any pending pin change
  triggerFlag = false;
  PCMSK |= (1 << PCINT3);
  GIMSK |= (1 << PCIE);
}

void handleTrigger() {
  triggerFlag = false;
  delay(20);
  if (!triggerHigh()) return;          // ignore noise
  triggerDisable();

  // wait for release (max 5 s)
  unsigned long t = millis();
  while (triggerHigh() && millis() - t < 5000) {}
  delay(500);

  flashStored();
  triggerEnable();
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

// ---- Deep sleep until readout trigger (watchdog off) ----
void sleepUntilTrigger() {
  wdt_disable();
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  cli();
  if (!triggerFlag) {
    sleep_enable();
    sei();
    sleep_cpu();
    sleep_disable();
  }
  sei();
}

void setup() {
  ledOff(LED_ONES);
  ledOff(LED_TENS);
  ledOff(LED_HUNDREDS);

  // ---- Power saving ----
  ADCSRA &= ~(1 << ADEN);   // ADC off (large drain in sleep if left on)
  ACSR   |=  (1 << ACD);    // analog comparator off
  power_adc_disable();
  power_usi_disable();      // hardware I2C unused (software I2C)

  bmp.begin();

  // Average baseline
  long sum = 0;
  for (int i = 0; i < 20; i++) {
    bmp.measure();
    sum += bmp.getPressurePa();
    delay(50);
  }
  baseline16 = (sum / 20) * 16;
  bmp.setBaselinePressure(sum / 20);

  bootFlash();       // sweep at boot
  triggerEnable();   // readout available from now on
}

void loop() {
  // ---- Readout request (any time except in flight) ----
  if (triggerFlag && !(launched && !landed)) {
    handleTrigger();
  }

  // ---- After landing: deep sleep until triggered ----
  if (landed) {
    sleepUntilTrigger();
    return;
  }

  // ---- Pre-launch ----
  if (!launched) {
    static uint8_t beatCounter = 0;

    bmp.measure(0x25);  // low-power reading
    long p = bmp.getPressurePa();
    long base = baseline16 >> 4;

    // launch = rapid 60 Pa drop below the slowly tracked baseline
    if (base - p > LAUNCH_DROP_PA) {
      launched = true;
      launchTime = millis();
      triggerDisable();
      bmp.setBaselinePressure(base);
      maxAltitude = bmp.getRelativeAltitudeM();
      return;
    }

    // baseline follows slow weather changes (~1 min time constant)
    baseline16 += ((p << 4) - baseline16) >> 6;

    // short heartbeat every ~10 s
    if (++beatCounter >= 10) {
      beatCounter = 0;
      heartbeat();
    }

    sleepSeconds(1);
    return;
  }

  // ---- In flight: sample at 20Hz for 6s ----
  if (millis() - launchTime <= FLIGHT_WINDOW_MS) {
    static unsigned long lastSample = 0;
    if (millis() - lastSample >= 50) {
      bmp.measure();
      float alt = bmp.getRelativeAltitudeM();
      if (alt > maxAltitude) maxAltitude = alt;
      lastSample = millis();
    }
    return;
  }

  // ---- Landed: save, flash once, then deep sleep ----
  landed = true;
  saveAltitude((int)(maxAltitude + 0.5f));
  flashStored();
  triggerEnable();
}
