#include <TinyWireM.h>

// ==== LED pins ====
#define LED_ONES     1   // PB1 = physical pin 6
#define LED_TENS     3   // PB3 = physical pin 2
#define LED_HUNDREDS 4   // PB4 = physical pin 3

#define BMP280_ADDR  0x76  // use 0x77 if SDO pulled high

// --- Calibration values ---
uint16_t dig_T1; int16_t dig_T2, dig_T3;
uint16_t dig_P1; int16_t dig_P2, dig_P3, dig_P4, dig_P5,
         dig_P6, dig_P7, dig_P8, dig_P9;

int32_t t_fine;

long baselineAltitude = 0;
long maxAltitude = 0;
long frozenMaxAltitude = -1;   // set after launch window ends

// timing
unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 1000; // 1s

// launch detection
bool launchDetected = false;
unsigned long launchStartTime = 0;

// idle heartbeat
unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 20000; // 20s

// ==== LED flash state machine ====
struct DigitFlash {
  int value;             
  int pin;               
  int flashesDone;       
  bool active;           
};

DigitFlash digits[3] = {
  {0, LED_HUNDREDS, 0, false},
  {0, LED_TENS,     0, false},
  {0, LED_ONES,     0, false}
};

int currentDigit = 0;
bool ledOn = false;
unsigned long lastBlinkTime = 0;
const unsigned long blinkOnTime = 200;
const unsigned long blinkOffTime = 200;
const unsigned long digitGap = 600;

// ==== I2C helpers ====
void bmpWrite8(uint8_t reg, uint8_t val) {
  TinyWireM.beginTransmission(BMP280_ADDR);
  TinyWireM.write(reg);
  TinyWireM.write(val);
  TinyWireM.endTransmission();
}

uint16_t bmpRead16(uint8_t reg) {
  TinyWireM.beginTransmission(BMP280_ADDR);
  TinyWireM.write(reg);
  TinyWireM.endTransmission();
  TinyWireM.requestFrom(BMP280_ADDR, (uint8_t)2);
  uint16_t val = TinyWireM.read();
  val |= (uint16_t)TinyWireM.read() << 8;
  return val;
}

int16_t bmpReadS16(uint8_t reg) {
  return (int16_t)bmpRead16(reg);
}

uint32_t bmpRead24(uint8_t reg) {
  TinyWireM.beginTransmission(BMP280_ADDR);
  TinyWireM.write(reg);
  TinyWireM.endTransmission();
  TinyWireM.requestFrom(BMP280_ADDR, (uint8_t)3);
  uint32_t val = ((uint32_t)TinyWireM.read() << 16) |
                 ((uint32_t)TinyWireM.read() << 8) |
                 TinyWireM.read();
  return val;
}

// ==== Compensation ====
void readCalibration() {
  dig_T1 = bmpRead16(0x88);
  dig_T2 = bmpReadS16(0x8A);
  dig_T3 = bmpReadS16(0x8C);
  dig_P1 = bmpRead16(0x8E);
  dig_P2 = bmpReadS16(0x90);
  dig_P3 = bmpReadS16(0x92);
  dig_P4 = bmpReadS16(0x94);
  dig_P5 = bmpReadS16(0x96);
  dig_P6 = bmpReadS16(0x98);
  dig_P7 = bmpReadS16(0x9A);
  dig_P8 = bmpReadS16(0x9C);
  dig_P9 = bmpReadS16(0x9E);
}

int32_t compensate_T(int32_t adc_T) {
  int32_t var1, var2, T;
  var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
  var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) *
            ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) *
          ((int32_t)dig_T3)) >> 14;
  t_fine = var1 + var2;
  T = (t_fine * 5 + 128) >> 8;
  return T;
}

uint32_t compensate_P(int32_t adc_P) {
  int64_t var1, var2, p;
  var1 = ((int64_t)t_fine) - 128000;
  var2 = var1 * var1 * (int64_t)dig_P6;
  var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
  var2 = var2 + (((int64_t)dig_P4) << 35);
  var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) +
         ((var1 * (int64_t)dig_P2) << 12);
  var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dig_P1) >> 33;
  if (var1 == 0) return 0;
  p = 1048576 - adc_P;
  p = (((p << 31) - var2) * 3125) / var1;
  var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
  var2 = (((int64_t)dig_P8) * p) >> 19;
  p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);
  return (uint32_t)p;
}

long readPressure() {
  int32_t adc_T = bmpRead24(0xFA);
  adc_T >>= 4;
  int32_t adc_P = bmpRead24(0xF7);
  adc_P >>= 4;
  compensate_T(adc_T);
  return (long)compensate_P(adc_P);
}

long readAltitude(long pressure) {
  return (long)(44330.0 * (1.0 - pow(pressure / 101325.0, 0.1903)));
}

// ==== LED non-blocking blink ====
void updateFlasher() {
  unsigned long now = millis();

  // if frozenMaxAltitude is set, blink that forever
  if (frozenMaxAltitude >= 0) {
    digits[0].value = (frozenMaxAltitude / 100) % 10;
    digits[1].value = (frozenMaxAltitude / 10) % 10;
    digits[2].value = frozenMaxAltitude % 10;
  }

  DigitFlash &d = digits[currentDigit];

  if (!d.active) {
    if (d.value == 0) {
      // skip this digit, move on after gap
      if (now - lastBlinkTime >= digitGap) {
        currentDigit++;
        if (currentDigit >= 3) currentDigit = 0;
        lastBlinkTime = now;
      }
      return;
    }
    d.active = true;
    d.flashesDone = 0;
    ledOn = false;
  }

  if (d.flashesDone < d.value) {
    if (!ledOn && now - lastBlinkTime >= blinkOffTime) {
      digitalWrite(d.pin, HIGH);
      ledOn = true;
      lastBlinkTime = now;
    } else if (ledOn && now - lastBlinkTime >= blinkOnTime) {
      digitalWrite(d.pin, LOW);
      ledOn = false;
      d.flashesDone++;
      lastBlinkTime = now;
    }
  } else {
    if (now - lastBlinkTime >= digitGap) {
      d.active = false;
      currentDigit++;
      if (currentDigit >= 3) currentDigit = 0;
      lastBlinkTime = now;
    }
  }
}

// flash all LEDs once
void flashAll() {
  digitalWrite(LED_ONES, HIGH);
  digitalWrite(LED_TENS, HIGH);
  digitalWrite(LED_HUNDREDS, HIGH);
  delay(200);
  digitalWrite(LED_ONES, LOW);
  digitalWrite(LED_TENS, LOW);
  digitalWrite(LED_HUNDREDS, LOW);
}

// ==== Setup ====
void setup() {
  TinyWireM.begin();
  pinMode(LED_ONES, OUTPUT);
  pinMode(LED_TENS, OUTPUT);
  pinMode(LED_HUNDREDS, OUTPUT);

  bmpWrite8(0xF4, 0x27);  // Normal mode, oversampling x1
  bmpWrite8(0xF5, 0xA0);
  readCalibration();

  baselineAltitude = readAltitude(readPressure());
  maxAltitude = 0;
  frozenMaxAltitude = -1;

  lastSensorRead = millis();
  lastHeartbeat = millis();
}

// ==== Loop ====
void loop() {
  unsigned long now = millis();

  // periodic sensor update
  if (now - lastSensorRead >= sensorInterval) {
    long p = readPressure();
    long currentAlt = readAltitude(p) - baselineAltitude;  // relative

    if (!launchDetected && currentAlt > 10) {
      launchDetected = true;
      launchStartTime = now;
      maxAltitude = currentAlt;
    }

    if (launchDetected && frozenMaxAltitude < 0) {
      if (currentAlt > maxAltitude) maxAltitude = currentAlt;

      if (now - launchStartTime >= 60000) {
        frozenMaxAltitude = maxAltitude; // lock it in
      } else {
        // still within launch window, show current max
        digits[0].value = (maxAltitude / 100) % 10;
        digits[1].value = (maxAltitude / 10) % 10;
        digits[2].value = maxAltitude % 10;
      }
    }

    lastSensorRead = now;
  }

  // idle heartbeat before launch
  if (!launchDetected && (now - lastHeartbeat >= heartbeatInterval)) {
    flashAll();
    lastHeartbeat = now;
  }

  updateFlasher();
}
