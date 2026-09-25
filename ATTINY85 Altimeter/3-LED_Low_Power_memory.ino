// set clock speed to 1mhz
// Software I2C on swapped pins: SDA = PB2 (physical pin 7), SCL = PB0 (physical pin 5)
// Power-on sequence: flashes the last saved flight altitude, then arms for launch
// Saved altitude is only overwritten when a new launch is detected
// After the flight window: permanent deep sleep until power is cycled
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <avr/power.h>
#include <avr/eeprom.h>

// ===== I2C pins (PORTB bit numbers) =====
#define I2C_SDA_BIT 2   // physical pin 7 (PB2)
#define I2C_SCL_BIT 0   // physical pin 5 (PB0)

// ===== LED pins (Arduino numbering, not physical pins) =====
#define LED_ONES     1   // physical pin 6 (PB1)
#define LED_TENS     3   // physical pin 2 (PB3)
#define LED_HUNDREDS 4   // physical pin 3 (PB4)

// ===== EEPROM layout =====
#define EE_MAGIC_ADDR ((uint8_t*)0)
#define EE_ALT_ADDR   ((uint16_t*)1)
#define EE_MAGIC      0xA5

// ===== Flight settings =====
#define LAUNCH_DROP_PA   60     // ~5 m (about 12 Pa per metre near sea level)
#define FLIGHT_WINDOW_MS 12000  // sample for 12 s after launch
#define SAVE_INTERVAL_MS 1000   // save max altitude to EEPROM every 1 s in flight
#define HEARTBEAT_WAKES  20     // heartbeat every 20 wakes (~20 s)

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
bool launched = false;
unsigned long launchTime = 0;
unsigned long lastSave = 0;
float maxAltitude = 0;
int savedAltitude = -1;
long baseline16 = 0;   // baseline pressure x16 (slowly follows weather)

// ===== LED functions =====
void allOff() {
  digitalWrite(LED_ONES, LOW);
  digitalWrite(LED_TENS, LOW);
  digitalWrite(LED_HUNDREDS, LOW);
}

void bootFlash() {
  digitalWrite(LED_ONES, HIGH); delay(50); digitalWrite(LED_ONES, LOW);
  digitalWrite(LED_TENS, HIGH); delay(50); digitalWrite(LED_TENS, LOW);
  digitalWrite(LED_HUNDREDS, HIGH); delay(50); digitalWrite(LED_HUNDREDS, LOW);
}

void heartbeat() {
  digitalWrite(LED_ONES, HIGH); delay(15); digitalWrite(LED_ONES, LOW);
}

void noDataFlash() {
  for (uint8_t i = 0; i < 3; i++) {
    digitalWrite(LED_ONES, HIGH);
    digitalWrite(LED_TENS, HIGH);
    digitalWrite(LED_HUNDREDS, HIGH);
    delay(15);
    allOff();
    delay(300);
  }
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

// ===== EEPROM =====
void saveAltitude(int alt) {
  if (alt < 0) alt = 0;
  if (alt == savedAltitude) return;      // avoid unnecessary writes
  eeprom_update_word(EE_ALT_ADDR, (uint16_t)alt);
  eeprom_update_byte(EE_MAGIC_ADDR, EE_MAGIC);
  savedAltitude = alt;
}

void flashStored() {
  if (eeprom_read_byte(EE_MAGIC_ADDR) != EE_MAGIC) {
    noDataFlash();   // no flight recorded yet
    return;
  }
  flashAltitude((int)eeprom_read_word(EE_ALT_ADDR));
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

// ---- Permanent deep sleep: no wake sources, only a power cycle restarts ----
void sleepForever() {
  allOff();
  wdt_disable();
  GIMSK = 0;
  PCMSK = 0;
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  cli();
  sleep_enable();
  sleep_cpu();
  while (1) {}
}

void setup() {
  pinMode(LED_ONES, OUTPUT);
  pinMode(LED_TENS, OUTPUT);
  pinMode(LED_HUNDREDS, OUTPUT);
  allOff();

  // ---- Power saving ----
  ADCSRA &= ~(1 << ADEN);   // ADC off (large drain in sleep if left on)
  ACSR   |=  (1 << ACD);    // analog comparator off
  power_adc_disable();
  power_usi_disable();      // hardware I2C unused (software I2C)

  // ---- Wake-up sequence: last saved flight altitude ----
  delay(500);
  flashStored();
  delay(1000);

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

  bootFlash();   // sweep = armed and watching for launch
}

void loop() {
  // ---- Pre-launch (watching) ----
  if (!launched) {
    static uint8_t beatCounter = 0;

    bmp.measure(0x25);  // low-power reading
    long p = bmp.getPressurePa();
    long base = baseline16 >> 4;

    // launch = rapid 60 Pa drop below the slowly tracked baseline
    if (base - p > LAUNCH_DROP_PA) {
      launched = true;
      launchTime = millis();
      bmp.setBaselinePressure(base);
      maxAltitude = bmp.getRelativeAltitudeM();
      saveAltitude((int)(maxAltitude + 0.5f));   // previous flight overwritten here
      lastSave = millis();
      return;
    }

    // baseline follows slow weather changes (~1 min time constant)
    baseline16 += ((p << 4) - baseline16) >> 6;

    // short heartbeat every ~20 s
    if (++beatCounter >= HEARTBEAT_WAKES) {
      beatCounter = 0;
      heartbeat();
    }

    sleepSeconds(1);
    return;
  }

  // ---- In flight: sample at 20Hz for 12s ----
  if (millis() - launchTime <= FLIGHT_WINDOW_MS) {
    static unsigned long lastSample = 0;
    if (millis() - lastSample >= 50) {
      bmp.measure();
      float alt = bmp.getRelativeAltitudeM();
      if (alt > maxAltitude) maxAltitude = alt;
      lastSample = millis();
    }

    // periodic save so a power loss in flight keeps the best value so far
    if (millis() - lastSave >= SAVE_INTERVAL_MS) {
      saveAltitude((int)(maxAltitude + 0.5f));
      lastSave = millis();
    }
    return;
  }

  // ---- Flight window over: final save, then sleep until power cycle ----
  saveAltitude((int)(maxAltitude + 0.5f));
  sleepForever();
}
