#include <TinyWireM.h>  // Lightweight I2C library for ATtiny

// ==== LED pins ====
#define LED_ONES     1   // PB1 = physical pin 6 → flashes ones digit
#define LED_TENS     3   // PB3 = physical pin 2 → flashes tens digit
#define LED_HUNDREDS 4   // PB4 = physical pin 3 → flashes hundreds digit

#define BMP280_ADDR  0x76  // I2C address of BMP280 (use 0x77 if SDO is pulled high)

// --- Calibration values from BMP280 registers ---
uint16_t dig_T1; int16_t dig_T2, dig_T3;
uint16_t dig_P1; int16_t dig_P2, dig_P3, dig_P4, dig_P5,
         dig_P6, dig_P7, dig_P8, dig_P9;

int32_t t_fine;  // temperature fine-tune variable used in pressure calc

// ==== Altitude tracking ====
long baselineAltitude = 0;       // Altitude at power-up (reference)
long maxAltitude = 0;            // Tracks maximum altitude during flight
long frozenMaxAltitude = -1;     // Set to maxAltitude after 60s, then flashed forever

// ==== Timing ====
unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 200; // sample every 200ms = 5Hz

// Launch detection flags
bool launchDetected = false;
unsigned long launchStartTime = 0;

// Heartbeat LED flash (before launch detect)
unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 2000; // every 2 seconds

// Startup settling time to avoid false launch detect
const unsigned long settleTime = 5000; // 5 seconds

// ==== LED flash state machine ====
struct DigitFlash {
  int value;             // number of flashes needed
  int pin;               // which LED pin (ignored if allLeds = true)
  int flashesDone;       // progress counter
  bool active;           // whether this digit is currently flashing
  bool allLeds;          // true if all LEDs flash together (thousands digit)
};

// Array of digits: [thousands, hundreds, tens, ones]
DigitFlash digits[4] = {
  {0, -1, 0, false, true},   // Thousands digit (special → flashes all LEDs)
  {0, LED_HUNDREDS, 0, false, false},
  {0, LED_TENS,     0, false, false},
  {0, LED_ONES,     0, false, false}
};

// Flash state machine control
int currentDigit = 0;               // which digit we are flashing now
bool ledOn = false;                 // is LED currently lit
unsigned long lastBlinkTime = 0;    // timestamp of last blink toggle
const unsigned long blinkOnTime = 200;   // LED ON time per blink (ms)
const unsigned long blinkOffTime = 200;  // gap between flashes of same digit
const unsigned long digitGap = 600;      // pause before next digit

// ==== I2C helpers for BMP280 ====
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

// ==== BMP280 calibration read ====
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

// ==== BMP280 compensation functions (from datasheet) ====
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
  if (var1 == 0) return 0; // avoid div by zero
  p = 1048576 - adc_P;
  p = (((p << 31) - var2) * 3125) / var1;
  var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
  var2 = (((int64_t)dig_P8) * p) >> 19;
  p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);
  return (uint32_t)p;
}

// ==== High-level sensor reads ====
long readPressure() {
  int32_t adc_T = bmpRead24(0xFA) >> 4; // temperature ADC
  int32_t adc_P = bmpRead24(0xF7) >> 4; // pressure ADC
  compensate_T(adc_T);                  // must run first
  return (long)compensate_P(adc_P);
}

long readAltitude(long pressure) {
  // Simple barometric formula, relative to sea-level pressure (101325 Pa)
  return (long)(44330.0 * (1.0 - pow(pressure / 101325.0, 0.1903)));
}

// ==== LED flash state machine update ====
void updateFlasher() {
  unsigned long now = millis();

  // Update digit values once frozen altitude is set
  if (frozenMaxAltitude >= 0) {
    digits[0].value = (frozenMaxAltitude / 1000) % 10; // thousands
    digits[1].value = (frozenMaxAltitude / 100) % 10;  // hundreds
    digits[2].value = (frozenMaxAltitude / 10) % 10;   // tens
    digits[3].value = frozenMaxAltitude % 10;          // ones
  }

  DigitFlash &d = digits[currentDigit];

  if (!d.active) {
    // If this digit has value = 0, skip it after a pause
    if (d.value == 0) {
      if (now - lastBlinkTime >= digitGap) {
        currentDigit++;
        if (currentDigit >= 4) currentDigit = 0;
        lastBlinkTime = now;
      }
      return;
    }
    // Start flashing this digit
    d.active = true;
    d.flashesDone = 0;
    ledOn = false;
  }

  if (d.flashesDone < d.value) {
    // Turn LED(s) ON
    if (!ledOn && now - lastBlinkTime >= blinkOffTime) {
      if (d.allLeds) {
        digitalWrite(LED_ONES, HIGH);
        digitalWrite(LED_TENS, HIGH);
        digitalWrite(LED_HUNDREDS, HIGH);
      } else {
        digitalWrite(d.pin, HIGH);
      }
      ledOn = true;
      lastBlinkTime = now;
    }
    // Turn LED(s) OFF
    else if (ledOn && now - lastBlinkTime >= blinkOnTime) {
      if (d.allLeds) {
        digitalWrite(LED_ONES, LOW);
        digitalWrite(LED_TENS, LOW);
        digitalWrite(LED_HUNDREDS, LOW);
      } else {
        digitalWrite(d.pin, LOW);
      }
      ledOn = false;
      d.flashesDone++;
      lastBlinkTime = now;
    }
  } else {
    // Move to next digit after a pause
    if (now - lastBlinkTime >= digitGap) {
      d.active = false;
      currentDigit++;
      if (currentDigit >= 4) currentDigit = 0;
      lastBlinkTime = now;
    }
  }
}

// ==== Flash all LEDs once (used for heartbeat) ====
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

  // Configure BMP280
  bmpWrite8(0xF4, 0x27);  // ctrl_meas: normal mode, oversampling x1
  bmpWrite8(0xF5, 0xA0);  // config: standby time & filter
  readCalibration();      // load calibration coefficients

  // Set baseline altitude at power-up
  baselineAltitude = readAltitude(readPressure());
  maxAltitude = 0;
  frozenMaxAltitude = -1;

  lastSensorRead = millis();
  lastHeartbeat = millis();
}

// ==== Main loop ====
void loop() {
  unsigned long now = millis();

  // ---- Sensor update every 200ms ----
  if (now - lastSensorRead >= sensorInterval) {
    long p = readPressure();
    long currentAlt = readAltitude(p) - baselineAltitude; // relative altitude

    // Launch detection (only after settleTime to avoid false trigger)
    if (!launchDetected && (now > settleTime) && currentAlt > 10) {
      launchDetected = true;
      launchStartTime = now;
      maxAltitude = currentAlt;
    }

    // Track maximum altitude until frozen
    if (launchDetected && frozenMaxAltitude < 0) {
      if (currentAlt > maxAltitude) maxAltitude = currentAlt;

      // After 60s of logging → freeze altitude
      if (now - launchStartTime >= 60000) {
        frozenMaxAltitude = maxAltitude;
        // (digits[] will get updated in updateFlasher)
      }
    }

    lastSensorRead = now;
  }

  // ---- Heartbeat (only before launch detect) ----
  if (!launchDetected && (now - lastHeartbeat >= heartbeatInterval)) {
    flashAll();
    lastHeartbeat = now;
  }

  // ---- Handle altitude flashing (after frozen) ----
  updateFlasher();
}
