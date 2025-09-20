#ifndef ATTINY85BMP280_H
#define ATTINY85BMP280_H

#include <Arduino.h>

class ATTINY85BMP280 {
  public:
    ATTINY85BMP280(uint8_t addr = 0x76);

    bool begin();
    void measure();
    long getPressurePa();
    void setBaselinePressure(long base);
    float getRelativeAltitudeM();
    float readTemperatureC();

  private:
    uint8_t i2caddr;
    long baseline;
    int32_t t_fine;

    // calibration values
    uint16_t dig_T1;
    int16_t  dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;

    // --- I²C bit-bang helpers ---
    void i2c_init();
    void i2c_start();
    void i2c_stop();
    bool i2c_write(uint8_t data);
    uint8_t i2c_read(bool ack);

    // --- BMP280 helpers ---
    uint8_t read8(uint8_t reg);
    uint16_t read16(uint8_t reg);
    uint32_t read24(uint8_t reg);
    void write8(uint8_t reg, uint8_t val);
};

#endif
