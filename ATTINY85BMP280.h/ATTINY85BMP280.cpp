#include "ATTINY85BMP280.h"

#define SDA_PIN PB0
#define SCL_PIN PB2

#define SDA_HIGH() DDRB &= ~(1 << SDA_PIN)
#define SDA_LOW()  DDRB |= (1 << SDA_PIN), PORTB &= ~(1 << SDA_PIN)
#define SCL_HIGH() DDRB &= ~(1 << SCL_PIN)
#define SCL_LOW()  DDRB |= (1 << SCL_PIN), PORTB &= ~(1 << SCL_PIN)

#define BMP280_REG_CHIPID   0xD0
#define BMP280_REG_RESET    0xE0
#define BMP280_REG_CTRL_MEAS 0xF4
#define BMP280_REG_CONFIG   0xF5
#define BMP280_REG_PRESSUREDATA 0xF7
#define BMP280_REG_TEMPDATA 0xFA
#define BMP280_REG_DIG_T1   0x88
#define BMP280_REG_DIG_P1   0x8E

ATTINY85BMP280::ATTINY85BMP280(uint8_t addr) {
  i2caddr = addr;
  baseline = 101325;
}

void ATTINY85BMP280::i2c_init() {
  SDA_HIGH();
  SCL_HIGH();
}

void ATTINY85BMP280::i2c_start() {
  SDA_HIGH(); SCL_HIGH(); delayMicroseconds(5);
  SDA_LOW();  delayMicroseconds(5);
  SCL_LOW();
}

void ATTINY85BMP280::i2c_stop() {
  SDA_LOW(); SCL_HIGH(); delayMicroseconds(5);
  SDA_HIGH(); delayMicroseconds(5);
}

bool ATTINY85BMP280::i2c_write(uint8_t data) {
  for (uint8_t i = 0; i < 8; i++) {
    if (data & 0x80) SDA_HIGH(); else SDA_LOW();
    data <<= 1;
    SCL_HIGH(); delayMicroseconds(5);
    SCL_LOW();  delayMicroseconds(5);
  }
  SDA_HIGH(); SCL_HIGH();
  bool ack = (PINB & (1 << SDA_PIN)) == 0;
  SCL_LOW();
  return ack;
}

uint8_t ATTINY85BMP280::i2c_read(bool ack) {
  uint8_t data = 0;
  SDA_HIGH();
  for (uint8_t i = 0; i < 8; i++) {
    data <<= 1;
    SCL_HIGH(); delayMicroseconds(5);
    if (PINB & (1 << SDA_PIN)) data |= 1;
    SCL_LOW();  delayMicroseconds(5);
  }
  if (ack) {
    SDA_LOW();
  } else {
    SDA_HIGH();
  }
  SCL_HIGH(); delayMicroseconds(5);
  SCL_LOW();
  SDA_HIGH();
  return data;
}

bool ATTINY85BMP280::begin() {
  i2c_init();
  if (read8(BMP280_REG_CHIPID) != 0x58) return false;

  write8(BMP280_REG_RESET, 0xB6);
  delay(100);

  // read calibration data
  dig_T1 = read16(BMP280_REG_DIG_T1);
  dig_T2 = (int16_t)read16(BMP280_REG_DIG_T1 + 2);
  dig_T3 = (int16_t)read16(BMP280_REG_DIG_T1 + 4);
  dig_P1 = read16(BMP280_REG_DIG_P1);
  dig_P2 = (int16_t)read16(BMP280_REG_DIG_P1 + 2);
  dig_P3 = (int16_t)read16(BMP280_REG_DIG_P1 + 4);
  dig_P4 = (int16_t)read16(BMP280_REG_DIG_P1 + 6);
  dig_P5 = (int16_t)read16(BMP280_REG_DIG_P1 + 8);
  dig_P6 = (int16_t)read16(BMP280_REG_DIG_P1 + 10);
  dig_P7 = (int16_t)read16(BMP280_REG_DIG_P1 + 12);
  dig_P8 = (int16_t)read16(BMP280_REG_DIG_P1 + 14);
  dig_P9 = (int16_t)read16(BMP280_REG_DIG_P1 + 16);

  write8(BMP280_REG_CTRL_MEAS, 0x27);
  write8(BMP280_REG_CONFIG, 0xA0);

  return true;
}

void ATTINY85BMP280::measure() {
  write8(BMP280_REG_CTRL_MEAS, 0x25);
  delay(10);
}

float ATTINY85BMP280::readTemperatureC() {
  int32_t adc_T = read24(BMP280_REG_TEMPDATA) >> 4;
  int32_t var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
  int32_t var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) *
                   ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) *
                   ((int32_t)dig_T3)) >> 14;
  t_fine = var1 + var2;
  float T = (t_fine * 5 + 128) >> 8;
  return T / 100;
}

long ATTINY85BMP280::getPressurePa() {
  readTemperatureC();
  int32_t adc_P = read24(BMP280_REG_PRESSUREDATA) >> 4;

  int64_t var1 = ((int64_t)t_fine) - 128000;
  int64_t var2 = var1 * var1 * (int64_t)dig_P6;
  var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
  var2 = var2 + (((int64_t)dig_P4) << 35);
  var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) +
         ((var1 * (int64_t)dig_P2) << 12);
  var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dig_P1) >> 33;

  if (var1 == 0) return 0;

  int64_t p = 1048576 - adc_P;
  p = (((p << 31) - var2) * 3125) / var1;
  var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
  var2 = (((int64_t)dig_P8) * p) >> 19;

  p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);

  return (long)(p / 256);
}

void ATTINY85BMP280::setBaselinePressure(long base) {
  baseline = base;
}

float ATTINY85BMP280::getRelativeAltitudeM() {
  long pressure = getPressurePa();
  return 44330.0 * (1.0 - pow(pressure / (float)baseline, 0.1903));
}

// ====== register read/write ======
uint8_t ATTINY85BMP280::read8(uint8_t reg) {
  i2c_start();
  i2c_write(i2caddr << 1);
  i2c_write(reg);
  i2c_start();
  i2c_write((i2caddr << 1) | 1);
  uint8_t val = i2c_read(false);
  i2c_stop();
  return val;
}

uint16_t ATTINY85BMP280::read16(uint8_t reg) {
  i2c_start();
  i2c_write(i2caddr << 1);
  i2c_write(reg);
  i2c_start();
  i2c_write((i2caddr << 1) | 1);
  uint8_t lo = i2c_read(true);
  uint8_t hi = i2c_read(false);
  i2c_stop();
  return (uint16_t)(lo | (hi << 8));
}

uint32_t ATTINY85BMP280::read24(uint8_t reg) {
  i2c_start();
  i2c_write(i2caddr << 1);
  i2c_write(reg);
  i2c_start();
  i2c_write((i2caddr << 1) | 1);
  uint8_t msb = i2c_read(true);
  uint8_t mid = i2c_read(true);
  uint8_t lsb = i2c_read(false);
  i2c_stop();
  return (uint32_t)msb << 16 | (uint32_t)mid << 8 | lsb;
}

void ATTINY85BMP280::write8(uint8_t reg, uint8_t val) {
  i2c_start();
  i2c_write(i2caddr << 1);
  i2c_write(reg);
  i2c_write(val);
  i2c_stop();
}
