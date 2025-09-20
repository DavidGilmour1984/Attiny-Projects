#include <ATTINY85BMP280.h>
#include <SoftwareSerial.h>

SoftwareSerial mySerial(3, 4); // RX, TX (PB3=pin2, PB4=pin3)
ATTINY85BMP280 bmp;

void setup() {
  mySerial.begin(2400);

  if (!bmp.begin()) {
    while (1); // stop if sensor not found
  }

  long sum = 0;
  for (int i = 0; i < 20; i++) {
    bmp.measure();
    sum += bmp.getPressurePa();
    delay(50);
  }
  bmp.setBaselinePressure(sum / 20);
}

void loop() {
  bmp.measure();
  float alt = bmp.getRelativeAltitudeM();
  mySerial.println(alt, 2); // numbers only
  delay(50);
}
